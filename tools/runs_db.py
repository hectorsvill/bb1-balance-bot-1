"""runs_db.py - a searchable catalog of every BB1 run, in one SQLite file.

Usage:
    tools/venv/bin/python tools/runs_db.py build              # add new or changed logs (fast after the first time)
    tools/venv/bin/python tools/runs_db.py build --rebuild    # start the catalog over
    tools/venv/bin/python tools/runs_db.py recent             # named queries: recent, longest, falls, by-gains
    tools/venv/bin/python tools/runs_db.py query "SELECT file, duration_s FROM runs WHERE surface = 'wood' AND ki = 100"
    tools/venv/bin/python tools/runs_db.py tables             # every table and column you can ask about

Why: 150+ logs and 300 MB of CSV are too many to open one by one. The catalog
has one row per log (what it was, the gains, how long, how it ended) and one
row per disarm, so "every wood run at ki 100 longer than 5 min, and how did it
end" is one SQL query.

The CSVs stay the source of truth: the catalog (logs/runs.sqlite) is built
from them, can be deleted and rebuilt any time, and is never committed
(bb1-private's .gitignore says *.sqlite). Mock logs are skipped, like
dataset.py. Parsing reuses dataset.py and check_dump.py, so every tool reads a
log the same way.

SQLite comes with Python (sqlite3), so nothing new to install.
"""

import argparse
import re
import sqlite3
from pathlib import Path

import pandas as pd

from check_dump import load_dump
from dataset import MODE_ARMED, MODE_FALLEN, REASON_OUTCOME, header_lines, segments

REPO = Path(__file__).resolve().parent.parent
DB = REPO / "logs" / "runs.sqlite"
LOOP_HZ = 200
SCHEMA_VERSION = 1          # bump when the tables change: build then starts over by itself

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

RUN_COLUMNS = (["file TEXT PRIMARY KEY", "started TEXT", "label TEXT", "surface TEXT", "note TEXT",
                "firmware TEXT", "firmware_version TEXT", "build_id TEXT", "log_format INTEGER", "gains_ver INTEGER"]
               + [f"{g} REAL" for g in GAIN_NAMES.values()]
               + ["rows INTEGER", "duration_s REAL", "armed_s REAL", "drive_s REAL", "disarm_count INTEGER",
                  "pitch_err_rms REAL", "pitch_max_abs REAL", "batt_v_start REAL", "batt_v_end REAL",
                  "outcome TEXT", "file_size INTEGER", "file_mtime REAL"])

# Named queries: the questions we ask most. "query" runs anything else.
QUERIES = {
    "recent": "SELECT started, label, surface, round(duration_s) AS s, round(armed_s) AS armed, outcome "
              "FROM runs ORDER BY started DESC LIMIT 15",
    "longest": "SELECT started, label, round(armed_s / 60, 1) AS armed_min, round(drive_s) AS drive_s, kp, ki, kd, outcome "
               "FROM runs ORDER BY armed_s DESC LIMIT 15",
    "falls": "SELECT r.started, d.file, round(d.t_s, 1) AS t_s, d.reason_text, r.kp, r.ki, r.kd, r.sp "
             "FROM disarms d JOIN runs r USING (file) WHERE d.reason = 2 ORDER BY r.started DESC, d.t_s LIMIT 20",
    "by-gains": "SELECT kp, ki, kd, sp, sdi, th, count(*) AS runs, round(sum(armed_s) / 60, 1) AS armed_min, "
                "sum(outcome = 'fell') AS fell, round(avg(pitch_err_rms), 2) AS avg_err "
                "FROM runs WHERE armed_s > 0 GROUP BY kp, ki, kd, sp, sdi, th ORDER BY armed_min DESC",
}


def connect(rebuild=False):
    db = sqlite3.connect(DB)
    version = db.execute("PRAGMA user_version").fetchone()[0]
    if rebuild or version != SCHEMA_VERSION:          # new or changed tables: start over
        db.executescript("DROP TABLE IF EXISTS runs; DROP TABLE IF EXISTS disarms;")
        db.execute(f"CREATE TABLE runs ({', '.join(RUN_COLUMNS)})")
        db.execute("CREATE TABLE disarms (file TEXT, t_s REAL, reason INTEGER, reason_text TEXT)")
        db.execute("CREATE INDEX disarms_file ON disarms (file)")
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


def summarize(path):
    """One log -> (runs row, disarm rows), or None for a mock log."""
    lines = header_lines(path)
    meta = dict(l[2:].split(",", 1) for l in lines if "," in l)    # first value per setting, as text
    if "MOCK" in meta.get("firmware", ""):
        return None
    started, label = name_parts(path)
    surface = meta.get("surface") or next((s for s in SURFACES if s in label), None)
    version = meta.get("firmware_version", "").split(",")          # "3.2,build_id,ota 8fb066a ..." (firmware 3.2 on)
    df, _ = load_dump(path)
    df["t"] = seconds(df)

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
            after = df.loc[g.index[-1] + 1:].head(5)     # the reason shows up a tick or two after the disarm
            codes = set(after.reason.astype(int)) - {0}
            code = 2 if MODE_FALLEN in set(after["mode"]) else min(codes, default=0)
            disarms.append((path.name, round(float(df.t[g.index[-1]]), 2), code, REASON_TEXT.get(code, "")))

    stat = path.stat()
    run = {"file": path.name, "started": started, "label": label, "surface": surface, "note": meta.get("note"),
           "firmware": meta.get("firmware"), "firmware_version": version[0] or None,
           "build_id": version[2] if len(version) > 2 else None,
           "log_format": int(number(meta.get("log_format", "").split(",")[0]) or 0) or None,
           **start_gains(lines),
           "rows": len(df), "duration_s": round(float(df.t.iloc[-1]), 2) if len(df) else 0,
           "armed_s": armed.sum() / LOOP_HZ, "drive_s": driving.sum() / LOOP_HZ, "disarm_count": len(disarms),
           "pitch_err_rms": round(float((err[armed] ** 2).mean() ** 0.5), 3) if armed.any() else None,
           "pitch_max_abs": round(float(df.pitch[armed].abs().max()), 2) if armed.any() else None,
           "batt_v_start": float(batt.iloc[0]) if len(batt) else None,
           "batt_v_end": float(batt.iloc[-1]) if len(batt) else None,
           "outcome": outcome, "file_size": stat.st_size, "file_mtime": stat.st_mtime}
    return run, disarms


def build(args):
    db = connect(args.rebuild)
    known = {f: (size, mtime) for f, size, mtime in db.execute("SELECT file, file_size, file_mtime FROM runs")}
    paths = sorted(Path(args.logs).glob("bb1_*.csv"))
    added = skipped = mock = 0
    for path in paths:
        stat = path.stat()
        if known.get(path.name) == (stat.st_size, stat.st_mtime):     # unchanged since the last build
            skipped += 1
            continue
        result = summarize(path)
        db.execute("DELETE FROM runs WHERE file = ?", (path.name,))   # a changed file replaces its old rows
        db.execute("DELETE FROM disarms WHERE file = ?", (path.name,))
        if result is None:
            mock += 1
            continue
        run, disarms = result
        db.execute(f"INSERT INTO runs ({', '.join(run)}) VALUES ({', '.join('?' * len(run))})", list(run.values()))
        db.executemany("INSERT INTO disarms VALUES (?, ?, ?, ?)", disarms)
        added += 1
        print(f"  {path.name}: {run['armed_s']:.0f} s armed, {run['outcome']}")
    gone = set(known) - {p.name for p in paths}                      # logs deleted or renamed since
    for f in gone:
        db.execute("DELETE FROM runs WHERE file = ?", (f,))
        db.execute("DELETE FROM disarms WHERE file = ?", (f,))
    db.commit()
    total = db.execute("SELECT count(*), sum(armed_s) / 60 FROM runs").fetchone()
    print(f"\n{added} added or updated, {skipped} unchanged, {mock} mock skipped, {len(gone)} removed. "
          f"Catalog: {total[0]} runs, {total[1] or 0:.0f} min armed, in {DB}")


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
    p.add_argument("--logs", default=str(REPO / "logs"), help="folder of logs (default logs/)")
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
