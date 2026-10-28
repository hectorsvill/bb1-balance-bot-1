"""runs_db.py - a searchable catalog of every BB1 run, in one SQLite file.

Usage:
    tools/venv/bin/python tools/runs_db.py build              # add new or changed logs (fast after the first time)
    tools/venv/bin/python tools/runs_db.py build --rebuild    # start the catalog over
    tools/venv/bin/python tools/runs_db.py recent             # named queries: recent, longest, falls, by-gains
    tools/venv/bin/python tools/runs_db.py query "SELECT file, duration_s FROM runs WHERE surface = 'wood' AND ki = 100"
    tools/venv/bin/python tools/runs_db.py tables             # every table and column you can ask about

Why: 150+ logs and 300 MB of CSV are too many to open one by one. Everything
we record goes in, each in a table shaped for how we use it:
  runs      one row per log: what it was, starting gains, how long, how it ended
  ticks     every telemetry row of every log (the raw columns, plus seconds into
            the file and wheel speed/turn in turns/s with the old 3.3x bug fixed)
  disarms   one row per disarm: when and why
  steps     one row per drive step (drive_test.py, drive_explore.py): asked vs got
  gains     every gains version a log used (live changes too), one row each
  meta      every "# name,value" header line, word for word (nothing is lost)
  flashes   logs/flash_history.csv: every firmware flash
  commands  logs/agent_commands.csv: every tools/bb1.py call (agent or by hand),
            linked to the log that was recording at the time, if any
Every table but flashes and commands links to runs by run_id, so "every wood run
at ki 100 longer than 5 min, and how did it end" is one SQL query, and so is
"the pitch of every fall's last second".

The CSVs stay the source of truth: the catalog (logs/runs.sqlite) is built
from them, can be deleted and rebuilt any time, and is never committed
(bb1-private's .gitignore says *.sqlite). Mock logs are skipped, like
dataset.py. Parsing reuses dataset.py and check_dump.py, so every tool reads a
log the same way.

SQLite comes with Python (sqlite3), so nothing new to install. With ticks the
file is a few hundred MB; build only reads logs that are new or changed.
"""

import argparse
import csv
import json
import re
import sqlite3
from datetime import datetime
from pathlib import Path

import pandas as pd

from check_dump import load_dump
from dataset import MODE_ARMED, MODE_FALLEN, OLD_SPEED_SCALE, header_lines, segments

REPO = Path(__file__).resolve().parent.parent
LOGS = REPO / "logs"
DB = LOGS / "runs.sqlite"
LOOP_HZ = 200
COUNTS_PER_REV = 520        # config.h
SCHEMA_VERSION = 2          # bump when the tables change: build then starts over by itself

# Reason codes in the log's reason column (control.h enum Reason; docs/CALIBRATION_AND_LOGS.md)
REASON_TEXT = {1: "disarmed by you", 2: "fell", 3: "IMU error", 4: "lost the page", 5: "loop late",
               6: "not upright", 7: "not calibrated", 8: "motor test", 9: "calibrating", 10: "page hidden",
               11: "arm time finished", 12: "picked up", 13: "battery low", 14: "firmware update"}

# The "# gains_ver,N,kp,18.000,..." line uses long names; the catalog uses the
# short names from serial, /gains and the pages, so a query reads like a setting.
GAIN_NAMES = {"kp": "kp", "ki": "ki", "kd": "kd", "trim": "trim", "pwm_cap": "cap", "deadband": "deadband",
              "speed_p": "sp", "speed_i": "si", "turn_hold": "th", "auto_arm": "auto", "drive_ff": "dff",
              "drive_speed_i": "sdi", "speed_mode": "smode", "wheel_speed_p": "wsp", "wheel_speed_i": "wsi",
              "i_relax": "irelax"}
SURFACES = ("wood", "carpet", "tile", "air")    # looked for in the file name when there's no "# surface" line

# Every column any log format has written (telemetry.h csvHeader(), docs/CALIBRATION_AND_LOGS.md).
# Older formats lack the later ones: those are empty (NULL) for their rows.
TICK_FIELDS = ["tick", "t_us", "work_us", "mode", "flag_bits", "ax_raw", "ay_raw", "az_raw", "temp_raw",
               "gx_raw", "gy_raw", "gz_raw", "pitch", "pitch_rate", "accel_pitch", "enc_l", "enc_r",
               "speed_l", "speed_r", "pwm_l", "pwm_r", "setpoint", "p_term", "i_term", "d_term", "gains_ver",
               "reason", "web_age_ms", "batt_v", "auto_state", "drive_speed", "drive_turn", "drive_speed_now",
               "drive_turn_now", "speed_pwm"]
# Added by us: seconds into the file, and the wheels in the same units as /drive
# (turns/s; turn = left minus right, right +), fixed for logs before format 10.
TICK_ADDED = ["t_s", "speed_tps", "turn_tps"]

TABLES = {
    "runs": ["run_id INTEGER PRIMARY KEY", "file TEXT UNIQUE", "started TEXT", "label TEXT", "surface TEXT",
             "note TEXT", "firmware TEXT", "firmware_version TEXT", "build_id TEXT", "log_format INTEGER",
             "gains_ver INTEGER"]
            + [f"{g} REAL" for g in GAIN_NAMES.values()]
            + ["rows INTEGER", "duration_s REAL", "armed_s REAL", "drive_s REAL", "disarm_count INTEGER",
               "pitch_err_rms REAL", "pitch_max_abs REAL", "batt_v_start REAL", "batt_v_end REAL",
               "outcome TEXT", "file_size INTEGER", "file_mtime REAL"],
    "ticks": ["run_id INTEGER"] + [f"{c} REAL" for c in TICK_FIELDS + TICK_ADDED],
    "disarms": ["run_id INTEGER", "t_s REAL", "tick INTEGER", "reason INTEGER", "reason_text TEXT"],
    "steps": ["run_id INTEGER", "n INTEGER", "name TEXT", "speed REAL", "turn REAL", "from_tick INTEGER",
              "to_tick INTEGER", "secs REAL", "got_speed REAL", "got_turn REAL"],
    "gains": ["run_id INTEGER", "ver INTEGER"] + [f"{g} REAL" for g in GAIN_NAMES.values()]
             + ["PRIMARY KEY (run_id, ver)"],
    "meta": ["run_id INTEGER", "line INTEGER", "name TEXT", "value TEXT"],
    "flashes": ["time TEXT", "version TEXT", "build_id TEXT", "method TEXT", "result TEXT"],
    "commands": ["time TEXT", "host TEXT", "cmd TEXT", "exit_code INTEGER", "ok INTEGER", "error TEXT",
                 "speed REAL", "turn REAL", "secs REAL", "moved_turns REAL", "turned_turns REAL",
                 "settled INTEGER", "run_id INTEGER", "args TEXT", "answer TEXT"],
}
PER_RUN = ("ticks", "disarms", "steps", "gains", "meta")   # rows that belong to one log

# Named queries: the questions we ask most. "query" runs anything else.
QUERIES = {
    "recent": "SELECT started, label, surface, round(duration_s) AS s, round(armed_s) AS armed, outcome "
              "FROM runs ORDER BY started DESC LIMIT 15",
    "longest": "SELECT started, label, round(armed_s / 60, 1) AS armed_min, round(drive_s) AS drive_s, kp, ki, kd, outcome "
               "FROM runs ORDER BY armed_s DESC LIMIT 15",
    "falls": "SELECT r.started, r.file, round(d.t_s, 1) AS t_s, d.reason_text, r.kp, r.ki, r.kd, r.sp "
             "FROM disarms d JOIN runs r USING (run_id) WHERE d.reason = 2 ORDER BY r.started DESC, d.t_s LIMIT 20",
    "by-gains": "SELECT kp, ki, kd, sp, sdi, th, count(*) AS runs, round(sum(armed_s) / 60, 1) AS armed_min, "
                "sum(outcome = 'fell') AS fell, round(avg(pitch_err_rms), 2) AS avg_err "
                "FROM runs WHERE armed_s > 0 GROUP BY kp, ki, kd, sp, sdi, th ORDER BY armed_min DESC",
    "steps": "SELECT r.started, r.surface, s.name, s.speed, s.turn, s.got_speed, s.got_turn, r.sdi, r.dff, r.th "
             "FROM steps s JOIN runs r USING (run_id) WHERE s.speed != 0 OR s.turn != 0 "
             "ORDER BY r.started DESC, s.n LIMIT 30",
    "commands": "SELECT time, cmd, speed, turn, secs, moved_turns, turned_turns, settled, ok, error "
                "FROM commands ORDER BY time DESC LIMIT 30",
}


def connect(rebuild=False):
    db = sqlite3.connect(DB)
    version = db.execute("PRAGMA user_version").fetchone()[0]
    if rebuild or version != SCHEMA_VERSION:          # new or changed tables: start over
        for t in db.execute("SELECT name FROM sqlite_master WHERE type = 'table'").fetchall():
            db.execute(f"DROP TABLE {t[0]}")
        for name, cols in TABLES.items():
            db.execute(f"CREATE TABLE {name} ({', '.join(cols)})")
        for t in PER_RUN:
            db.execute(f"CREATE INDEX {t}_run ON {t} (run_id)")
        db.execute("CREATE INDEX ticks_tick ON ticks (run_id, tick)")
        db.execute(f"PRAGMA user_version = {SCHEMA_VERSION}")
    return db


def number(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def start_gains(lines):
    """The "# gains_ver,N,name,value,..." line: the gains when the header was written
    (the start for logger.py and the test tools, save time for the page's last 10 s)."""
    for l in lines:
        if l.startswith("# gains_ver,"):
            parts = [p.strip() for p in l[2:].split(",")]
            values = dict(zip(parts[2::2], parts[3::2]))
            gains = {short: number(values.get(long)) for long, short in GAIN_NAMES.items()}
            gains["gains_ver"] = int(number(parts[1]) or 0)
            return gains
    return {short: None for short in GAIN_NAMES.values()} | {"gains_ver": None}


def gain_versions(lines, start):
    """Every gains version the log used: the start line plus each "# gains_vN,kp 18 ki 100 ..."
    line (one per version seen while recording, short names, space separated)."""
    found = {}
    if start["gains_ver"] is not None:
        found[start["gains_ver"]] = {g: start[g] for g in GAIN_NAMES.values()}
    for l in lines:
        m = re.match(r"# gains_v(\d+),(.*)", l)
        if m:
            words = m.group(2).split()
            values = dict(zip(words[0::2], words[1::2]))
            found[int(m.group(1))] = {g: number(values.get(g)) for g in GAIN_NAMES.values()}
    return found


def seconds(df):
    """Seconds into the file for every row. Counted from tick steps, not t_us:
    if BB1 restarted during the recording, its clock (and tick) start again
    from 0 and t_us - first would go negative. A backward jump counts as one tick."""
    steps = df.tick.diff().fillna(0).clip(lower=1)
    steps.iloc[0] = 0
    return steps.cumsum() / LOOP_HZ


def name_parts(path):
    """bb1_2026-10-26_23-49-46_train-v26-wood-explore-p3.csv -> start time, label."""
    m = re.match(r"bb1_(\d{4}-\d\d-\d\d)_(\d\d)-(\d\d)-(\d\d)_?(.*)\.csv$", path.name)
    if not m:
        return None, path.stem
    day, h, mi, s, label = m.groups()
    return f"{day} {h}:{mi}:{s}", label


def drive_steps(lines, df):
    """The "# step,name,speed,turn,from_tick,to_tick" lines, plus what the wheels
    did: the average over the step's last half (same as drive_test.py's report)."""
    rows = []
    for n, l in enumerate(l for l in lines if l.startswith("# step,")):
        parts = l[2:].split(",")
        name = ",".join(parts[1:-4])                    # a name could hold a comma
        speed, turn, a, b = (number(x) for x in parts[-4:])
        if a is None or b is None:
            continue
        mid = a + (b - a) / 2
        tail = df[(df.tick >= mid) & (df.tick <= b)]
        rows.append((n, name, speed, turn, int(a), int(b), (b - a) / LOOP_HZ,
                     round(float(tail.speed_tps.mean()), 3) if len(tail) else None,
                     round(float(tail.turn_tps.mean()), 3) if len(tail) else None))
    return rows


def summarize(path):
    """One log -> a dict of rows for each table, or None for a mock log."""
    lines = header_lines(path)
    meta = dict(l[2:].split(",", 1) for l in lines if "," in l)    # first value per setting, as text
    if "MOCK" in meta.get("firmware", ""):
        return None
    started, label = name_parts(path)
    surface = meta.get("surface") or next((s for s in SURFACES if s in label), None)
    version = meta.get("firmware_version", "").split(",")          # "3.2,build_id,ota 8fb066a ..." (firmware 3.2 on)
    log_format = int(number(meta.get("log_format", "").split(",")[0]) or 0) or None
    df, _ = load_dump(path)
    df["t"] = seconds(df)

    # Wheels in turns/s like /drive. speed_l/speed_r read 3.3x low before format 10 (constrain() bug)
    scale = OLD_SPEED_SCALE if (log_format or 0) < 10 else 1
    if "speed_l" in df:
        df["speed_tps"] = (df.speed_l + df.speed_r) / 2 * scale / COUNTS_PER_REV
        df["turn_tps"] = (df.speed_l - df.speed_r) * scale / COUNTS_PER_REV
    else:
        df["speed_tps"] = df["turn_tps"] = None

    armed = df["mode"] == MODE_ARMED if "mode" in df else pd.Series(False, index=df.index)
    # How far from the target it leaned: pitch - setpoint (logs before format 3 have no setpoint: pitch alone)
    err = (df.pitch - df.setpoint) if "setpoint" in df else df.pitch
    driving = ((df.drive_speed != 0) | (df.drive_turn != 0)) if "drive_speed" in df else pd.Series(False, index=df.index)
    batt = df.batt_v[df.batt_v > 1] if "batt_v" in df else pd.Series(dtype=float)   # 0 = no sense wire

    disarms, outcome = [], "never_armed"
    if armed.any() and "reason" in df:
        for g, outcome in segments(df):                  # same armed stretches and outcomes as dataset.py
            if outcome == "log_ended":
                continue
            last = g.index[-1]
            after = df.loc[last + 1:].head(5)            # the reason shows up a tick or two after the disarm
            codes = set(after.reason.astype(int)) - {0}
            code = 2 if MODE_FALLEN in set(after["mode"]) else min(codes, default=0)
            disarms.append((round(float(df.t[last]), 2), int(df.tick[last]), code, REASON_TEXT.get(code, "")))

    start = start_gains(lines)
    stat = path.stat()
    run = {"file": path.name, "started": started, "label": label, "surface": surface, "note": meta.get("note"),
           "firmware": meta.get("firmware"), "firmware_version": version[0] or None,
           "build_id": version[2] if len(version) > 2 else None, "log_format": log_format, **start,
           "rows": len(df), "duration_s": round(float(df.t.iloc[-1]), 2) if len(df) else 0,
           "armed_s": armed.sum() / LOOP_HZ, "drive_s": driving.sum() / LOOP_HZ, "disarm_count": len(disarms),
           "pitch_err_rms": round(float((err[armed] ** 2).mean() ** 0.5), 3) if armed.any() else None,
           "pitch_max_abs": round(float(df.pitch[armed].abs().max()), 2) if armed.any() else None,
           "batt_v_start": float(batt.iloc[0]) if len(batt) else None,
           "batt_v_end": float(batt.iloc[-1]) if len(batt) else None,
           "outcome": outcome, "file_size": stat.st_size, "file_mtime": stat.st_mtime}

    ticks = df.rename(columns={"t": "t_s"}).reindex(columns=TICK_FIELDS + TICK_ADDED)
    ticks = ticks.astype(object).where(ticks.notna(), None)        # NaN -> NULL
    return {"run": run, "ticks": ticks.itertuples(index=False, name=None), "disarms": disarms,
            "steps": drive_steps(lines, df),
            "gains": [(v, *g.values()) for v, g in gain_versions(lines, start).items()],
            "meta": [(i, *(l[2:].split(",", 1) + [""])[:2]) for i, l in enumerate(lines)]}


def forget(db, file):
    """Drop one log's rows from every table (it changed, or it's gone)."""
    row = db.execute("SELECT run_id FROM runs WHERE file = ?", (file,)).fetchone()
    if row:
        for t in PER_RUN:
            db.execute(f"DELETE FROM {t} WHERE run_id = ?", row)
        db.execute("DELETE FROM runs WHERE run_id = ?", row)


def insert(db, table, rows, width=None):
    rows = list(rows) if width is None else rows
    width = width or (len(rows[0]) if rows else 0)
    if width:
        db.executemany(f"INSERT INTO {table} VALUES ({', '.join('?' * width)})", rows)


def local_time(text):
    """'2026-10-28T09:52:44.89-04:00' -> '2026-10-28 09:52:44.890', BB1's local
    time like runs.started, so the two compare as text."""
    try:
        return datetime.fromisoformat(text).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    except (TypeError, ValueError):
        return text


def load_side_files(db):
    """flash_history.csv and agent_commands.csv: small, so read again every build."""
    db.execute("DELETE FROM flashes")
    flashes = LOGS / "flash_history.csv"
    if flashes.exists():
        with open(flashes) as f:
            insert(db, "flashes", [(r["time"], r["version"], r["build_id"], r["method"], r["result"])
                                   for r in csv.DictReader(f)])
    db.execute("DELETE FROM commands")
    commands = LOGS / "agent_commands.csv"
    rows = []
    if commands.exists():
        with open(commands) as f:
            for r in csv.DictReader(f):
                try:
                    args, answer = json.loads(r["args"] or "{}"), json.loads(r["answer"] or "{}")
                except ValueError:                       # a half-written line: keep the raw text
                    args, answer = {}, {}
                asked = answer.get("asked") or {}
                rows.append((local_time(r["time"]), r["host"], r["cmd"], number(r["exit_code"]),
                             answer.get("ok"), answer.get("error"),
                             asked.get("speed", args.get("speed")), asked.get("turn", args.get("turn")),
                             asked.get("secs", args.get("secs")), answer.get("moved_turns"),
                             answer.get("turned_turns"), answer.get("settled"), None, r["args"], r["answer"]))
    insert(db, "commands", rows)
    # Which log was recording when the command ran (if any): its start plus its length
    db.execute("UPDATE commands SET run_id = (SELECT run_id FROM runs WHERE started IS NOT NULL "
               "AND commands.time >= started AND commands.time <= datetime(started, '+' || duration_s || ' seconds') "
               "ORDER BY started DESC LIMIT 1)")
    return len(rows)


def add(db, path):
    """Put one log (new or changed) in the catalog. Returns its runs row, or None for a mock log."""
    forget(db, path.name)                                              # a changed file replaces its old rows
    result = summarize(path)
    if result is None:
        return None
    run = result["run"]
    cur = db.execute(f"INSERT INTO runs ({', '.join(run)}) VALUES ({', '.join('?' * len(run))})",
                     list(run.values()))
    rid = cur.lastrowid
    insert(db, "ticks", ((rid, *r) for r in result["ticks"]), width=1 + len(TICK_FIELDS + TICK_ADDED))
    for t in ("disarms", "steps", "gains", "meta"):
        insert(db, t, [(rid, *r) for r in result[t]])
    db.commit()                                                        # one log at a time: a crash keeps the rest
    return run | {"run_id": rid}


def catalog(path):
    """Called by the recorders (logger.save_log) right after a log is saved, so the
    catalog is always current. Only logs in logs/ go in (a scratch or mock folder
    never does). Never raises: a catalog problem must not lose a recording."""
    global DB
    path = Path(path)
    try:
        if path.resolve().parent != LOGS.resolve():
            return
        DB = LOGS / "runs.sqlite"
        db = connect()
        run = add(db, path)
        load_side_files(db)
        db.commit()
        db.close()
        if run:
            print(f"Catalogued as run {run['run_id']} in {DB.name} (tools/logs.py show {run['run_id']})")
    except Exception as e:                                             # the CSV is saved; build can catch up later
        print(f"Not catalogued ({e}): run 'tools/venv/bin/python tools/runs_db.py build' later")


def build(args):
    global LOGS, DB
    LOGS = Path(args.logs)
    DB = LOGS / "runs.sqlite"                  # the catalog lives next to the logs it was built from
    db = connect(args.rebuild)
    known = {f: (size, mtime) for f, size, mtime in db.execute("SELECT file, file_size, file_mtime FROM runs")}
    paths = sorted(LOGS.glob("bb1_*.csv"))
    added = skipped = mock = 0
    for path in paths:
        stat = path.stat()
        if known.get(path.name) == (stat.st_size, stat.st_mtime):     # unchanged since the last build
            skipped += 1
            continue
        run = add(db, path)
        if run is None:
            mock += 1
            continue
        added += 1
        print(f"  {path.name}: {run['rows']} ticks, {run['armed_s']:.0f} s armed, {run['outcome']}")
    gone = set(known) - {p.name for p in paths}                      # logs deleted or renamed since
    for f in gone:
        forget(db, f)
    n_commands = load_side_files(db)
    db.commit()
    total = db.execute("SELECT count(*), sum(armed_s) / 60, sum(rows) FROM runs").fetchone()
    print(f"\n{added} added or updated, {skipped} unchanged, {mock} mock skipped, {len(gone)} removed. "
          f"Catalog: {total[0]} runs, {total[2] or 0:,} ticks, {total[1] or 0:.0f} min armed, "
          f"{n_commands} bb1.py commands, in {DB}")


def show(sql):
    """Run one query and print it as a table."""
    if not DB.exists():
        raise SystemExit(f"No catalog yet: run 'tools/venv/bin/python tools/runs_db.py build' first.")
    with sqlite3.connect(DB) as db:
        try:
            table = pd.read_sql_query(sql, db)
        except Exception as e:                           # bad SQL: say why instead of a traceback
            raise SystemExit(f"Query failed: {e}")
    with pd.option_context("display.max_rows", 500, "display.width", 200, "display.max_columns", 40):
        print(table.to_string(index=False) if len(table) else "(no rows)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("build", help="add new or changed logs to the catalog")
    p.add_argument("--rebuild", action="store_true", help="start over from every log")
    p.add_argument("--logs", default=str(LOGS), help="folder of logs; the catalog goes in it too (default logs/)")
    p = sub.add_parser("query", help="run any SQL and print the result")
    p.add_argument("sql")
    sub.add_parser("tables", help="list the tables and their columns")
    for name in QUERIES:
        sub.add_parser(name, help=f"named query: {QUERIES[name][:60]}...")
    args = ap.parse_args()

    if args.cmd == "build":
        build(args)
    elif args.cmd == "query":
        show(args.sql)
    elif args.cmd == "tables":
        show("SELECT m.name AS tbl, p.name AS col, p.type FROM sqlite_master m, pragma_table_info(m.name) p "
             "WHERE m.type = 'table' ORDER BY m.name, p.cid")
    else:
        show(QUERIES[args.cmd])


if __name__ == "__main__":
    main()
