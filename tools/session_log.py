"""session_log.py - record BB1's telemetry automatically while an agent drives it.

You don't run this by hand: tools/bb1.py starts it in the background the first
time BB1 is balancing after an `arm` or a `drive`, and `bb1.py log` controls it:

    tools/venv/bin/python tools/bb1.py log status                      # recording? how much so far
    tools/venv/bin/python tools/bb1.py log start --surface carpet -n "square test"   # label it (or start it)
    tools/venv/bin/python tools/bb1.py log stop                        # save now

It does what tools/logger.py does (every 5 ms record from GET /log, the same
file layout, saved with logger.save_log), so the log lands in logs/ and in
logs/runs.sqlite like every other run, and runs_db links the session's
bb1.py commands to it (the `commands` table's run_id).

Why a separate process: each bb1.py call is a short program that exits, but
BB1 only keeps the last 10 s of records. Something has to keep reading /log
between commands, or the data is gone.

It stops and saves by itself when:
  - BB1 has been disarmed for DISARMED_STOP_S and no bb1.py command came in
    that time (so the fall after a disarm is still in the log)
  - no bb1.py command for IDLE_STOP_S (the agent is done)
  - BB1 hasn't answered for NO_ANSWER_STOP_S
  - it has run MAX_RECORD_S (the next command starts a new log)
  - `bb1.py log stop`, or it is sent SIGTERM / Ctrl-C

Its working files live in logs/.recorder/ (gitignored in bb1-private):
pid, status.json (updated every 2 s), label.json (note and surface for the
next save), last.json (the last saved log), out.txt (what it printed).
Calls to the mock (localhost) are never recorded: mock data never mixes with
real data.
"""

import argparse
import json
import os
import signal
import sqlite3
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from logger import MAX_PER_REQUEST, fetch_log, gains_versions, get, log_path, save_log

REPO = Path(__file__).resolve().parent.parent
DIR = REPO / "logs" / ".recorder"
PID, STATUS, LABEL, LAST, OUT = (DIR / n for n in ("pid", "status.json", "label.json", "last.json", "out.txt"))
LAST_COMMAND = DIR / "last_command"      # bb1.py touches it on every command: its mtime = the last one

LOOKBACK_TICKS = 400       # start 2 s back, so the arm or the start of the drive is in the log
DISARMED_STOP_S = 10
IDLE_STOP_S = 300
NO_ANSWER_STOP_S = 30
MAX_RECORD_S = 1800        # 30 min, about 60 MB of CSV
STATUS_EVERY_S = 2


# ---------- used by bb1.py ----------

def running_pid():
    """The recorder's process id, or None. Cleans up after one that died."""
    try:
        pid = int(PID.read_text())
        os.kill(pid, 0)                    # signal 0 = "are you there?", sends nothing
        return pid
    except (OSError, ValueError):
        PID.unlink(missing_ok=True)
        return None


def touch_command():
    """bb1.py calls this on every command, so the recorder knows the agent is still busy."""
    DIR.mkdir(parents=True, exist_ok=True)
    LAST_COMMAND.touch()


def set_label(note=None, surface=None):
    """Note and surface for the next save (a running recorder reads them when it saves)."""
    DIR.mkdir(parents=True, exist_ok=True)
    label = read_json(LABEL)
    if note is not None:
        label["note"] = note
    if surface is not None:
        label["surface"] = surface
    LABEL.write_text(json.dumps(label))
    return label


def ensure(base, host):
    """Start the recorder if it isn't running. Returns (pid, started_now)."""
    pid = running_pid()
    if pid:
        return pid, False
    DIR.mkdir(parents=True, exist_ok=True)
    with open(OUT, "a") as out:
        proc = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "--base", base, "--host", host],
                                stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
                                start_new_session=True,            # not killed with the bb1.py that started it
                                cwd=REPO)
    PID.write_text(str(proc.pid))
    return proc.pid, True


def stop(wait_s=15):
    """Ask the recorder to save and stop; wait for it. Returns last.json, or None if none was running."""
    pid = running_pid()
    if not pid:
        return None
    os.kill(pid, signal.SIGTERM)
    end = time.time() + wait_s
    while time.time() < end and running_pid():
        time.sleep(0.2)
    return read_json(LAST)


def status():
    pid = running_pid()
    return {"recording": bool(pid), **(read_json(STATUS) if pid else {}), "last_saved": read_json(LAST) or None}


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


# ---------- the recorder process ----------

stop_asked = False


def ask_stop(signum, frame):
    global stop_asked
    stop_asked = True


def seconds_since_command():
    try:
        return time.time() - LAST_COMMAND.stat().st_mtime
    except OSError:
        return 0.0


def record(base, host):
    signal.signal(signal.SIGTERM, ask_stop)
    signal.signal(signal.SIGINT, ask_stop)
    started = datetime.now()
    meta = get(f"{base}/log/meta")
    state = json.loads(get(f"{base}/state"))
    next_tick = max(0, state["tick"] - LOOKBACK_TICKS)
    versions = {}
    gains_versions(json.dumps(state), versions)
    rows, lost, header = [], 0, ""
    armed, disarmed_since = state["armed"], None if state["armed"] else time.time()
    last_answer = last_status = time.time()
    t0 = time.time()
    why = None
    print(f"{started:%Y-%m-%d %H:%M:%S} recording {host} ({base}), pid {os.getpid()}", flush=True)

    while why is None:
        if stop_asked:
            why = "asked to stop"
            break
        try:
            info, header, new = fetch_log(base, next_tick)
            rows += new
            lost += info["lost"]
            next_tick = info["to"]
            last_answer = time.time()
        except OSError:
            if time.time() - last_answer > NO_ANSWER_STOP_S:
                why = f"no answer from BB1 for {NO_ANSWER_STOP_S} s"
            time.sleep(0.5)
            continue

        if time.time() - last_status > STATUS_EVERY_S:
            last_status = time.time()
            try:
                s = get(f"{base}/state", timeout=3)
                gains_versions(s, versions)                 # gains changed on the page go in the file too
                armed = json.loads(s)["armed"]
            except (OSError, ValueError):
                pass
            disarmed_since = None if armed else (disarmed_since or time.time())
            idle = seconds_since_command()
            if disarmed_since and time.time() - disarmed_since > DISARMED_STOP_S and idle > DISARMED_STOP_S:
                why = f"disarmed for {DISARMED_STOP_S} s"
            elif idle > IDLE_STOP_S:
                why = f"no bb1.py command for {IDLE_STOP_S} s"
            elif time.time() - t0 > MAX_RECORD_S:
                why = f"{MAX_RECORD_S // 60} min limit"
            STATUS.write_text(json.dumps({"pid": os.getpid(), "host": host, "started": started.isoformat(timespec="seconds"),
                                          "seconds": round(len(rows) / 200, 1), "records": len(rows), "lost": lost,
                                          "armed": armed}))
        if info["to"] - info["from"] < MAX_PER_REQUEST:
            time.sleep(0.25)                                # caught up: wait a little, like logger.py

    label = read_json(LABEL)
    note = label.get("note") or "agent session"
    surface = label.get("surface") or os.environ.get("BB1_SURFACE", "unknown")
    saved = {"stopped": why, "records": len(rows), "lost": lost, "seconds": round(len(rows) / 200, 1)}
    if rows:
        out = log_path(note, started)
        save_log(out, meta, note, started, header, rows, lost, versions, surface)   # also catalogs it in runs.sqlite
        saved.update(file=out.name, run_id=run_id_of(out), surface=surface, note=note)
    LAST.write_text(json.dumps(saved))
    LABEL.unlink(missing_ok=True)                           # the label was for this log
    STATUS.unlink(missing_ok=True)
    print(f"{datetime.now():%H:%M:%S} stopped: {why}; {saved}", flush=True)


def run_id_of(path):
    """The run_id runs_db gave the file we just saved (None if it wasn't catalogued)."""
    try:
        db = sqlite3.connect(f"file:{path.parent / 'runs.sqlite'}?mode=ro", uri=True)
        row = db.execute("SELECT run_id FROM runs WHERE file = ?", (path.name,)).fetchone()
        db.close()
        return row[0] if row else None
    except sqlite3.Error:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="http://<ip>[:port], already looked up by bb1.py")
    ap.add_argument("--host", required=True, help="the name the agent used (for the status)")
    args = ap.parse_args()
    PID.write_text(str(os.getpid()))
    try:
        record(args.base, args.host)
    finally:
        if PID.exists() and PID.read_text().strip() == str(os.getpid()):
            PID.unlink()


if __name__ == "__main__":
    main()
