"""logs.py - look at, slice and download BB1's logs. Read only. JSON out, for AI agents and scripts.

Usage:
    tools/venv/bin/python tools/logs.py schema                     # every table, what it's for, its columns
    tools/venv/bin/python tools/logs.py runs --limit 10            # newest runs (--where "surface = 'carpet'")
    tools/venv/bin/python tools/logs.py show 158                   # one run: summary, disarms, steps, gains, header
    tools/venv/bin/python tools/logs.py ticks 158 --cols pitch,speed_tps --from 5 --to 10
    tools/venv/bin/python tools/logs.py download 158 --out run.csv # the original CSV (or --cols ... for a slice)
    tools/venv/bin/python tools/logs.py sql "SELECT surface, count(*) FROM runs GROUP BY surface"
    tools/venv/bin/python tools/logs.py serve                      # the same on a web page: http://localhost:8090
    tools/venv/bin/python tools/logs.py --db tools/tests/fixtures/runs.sqlite runs   # another catalog (tests)

A run is its run_id, its file name, or any part of the file name that matches
only one log ("sn3-agent"). run_ids can change after 'runs_db.py build --rebuild';
the file name never does, so keep the file name when you note a run down.

Everything comes from logs/runs.sqlite, which tools/runs_db.py builds from the
CSVs and every recorder updates after it saves a log. This tool opens it read
only: nothing here can change or delete data, so an agent can call it freely.
Answers are one JSON document on stdout; errors are {"ok": false, "error": ...}
with exit code 1. Big answers are thinned, not cut silently: 'ticks' returns at
most --limit rows (default 2000) and says which every-Nth row it kept.
"""

import argparse
import csv
import io
import json
import shutil
import sqlite3
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGS = REPO / "logs"
DB = LOGS / "runs.sqlite"
PAGE = Path(__file__).resolve().parent / "logs_page.html"
MAX_ROWS = 2000          # default cap on rows per answer: enough for a plot, small enough for an agent to read

# What each table is for (runs_db.py makes them). Shown by 'schema' so an agent can plan a query.
TABLE_USE = {
    "runs": "one row per log: what it was, starting gains, duration, time armed/driving, outcome",
    "ticks": "every 5 ms telemetry record; t_s = seconds into the file, speed_tps/turn_tps = wheels in turns/s",
    "disarms": "one row per disarm: t_s, tick (last armed tick), reason code and text (2 = fell)",
    "steps": "one row per drive step: asked speed/turn vs got_speed/got_turn (turns/s, step's last half)",
    "gains": "every gains version a log used (live changes too)",
    "meta": "every '# name,value' header line of a log, word for word",
    "flashes": "every firmware flash (logs/flash_history.csv)",
    "commands": "every tools/bb1.py call: asked, moved_turns, settled, raw JSON; run_id = log recording then",
}
RUN_LIST = ("run_id, file, started, label, surface, note, round(duration_s, 1) AS duration_s, "
            "round(armed_s, 1) AS armed_s, round(drive_s, 1) AS drive_s, disarm_count, outcome, "
            "kp, ki, kd, sp, sdi, th")


class Refused(Exception):
    """A request we can't answer (bad run, bad column, bad SQL)."""


def use_db(path):
    """Read another catalog (the tests' fixture one, say) instead of logs/runs.sqlite.
    Its logs are the CSVs next to it, like 'runs_db.py build --logs <folder>' makes it."""
    global DB, LOGS
    DB = Path(path).resolve()
    LOGS = DB.parent


def open_db():
    if not DB.exists():
        raise Refused("no logs/runs.sqlite yet: run 'tools/venv/bin/python tools/runs_db.py build'")
    db = sqlite3.connect(f"file:{DB}?mode=ro", uri=True, check_same_thread=False)   # read only
    db.row_factory = sqlite3.Row
    return db


def rows(db, sql, params=()):
    try:
        return [dict(r) for r in db.execute(sql, params)]
    except (sqlite3.Error, sqlite3.Warning) as e:   # Warning: more than one statement
        raise Refused(f"SQL: {e}")


def find_run(db, which):
    """run_id, file name, or a unique part of the file name -> the runs row."""
    which = str(which)
    if which.isdigit():
        found = rows(db, "SELECT * FROM runs WHERE run_id = ?", (int(which),))
    else:
        found = rows(db, "SELECT * FROM runs WHERE file = ?", (which,)) or \
            rows(db, "SELECT * FROM runs WHERE file LIKE ? ORDER BY started", (f"%{which}%",))
    if not found:
        raise Refused(f"no run matches {which!r}: try 'runs'")
    if len(found) > 1:
        raise Refused(f"{len(found)} runs match {which!r}: " + ", ".join(r["file"] for r in found[:10]))
    return found[0]


def schema(db):
    tables = {}
    for t in rows(db, "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"):
        cols = [c["name"] for c in rows(db, f"SELECT name FROM pragma_table_info('{t['name']}')")]
        count = db.execute(f"SELECT count(*) FROM {t['name']}").fetchone()[0]
        tables[t["name"]] = {"use": TABLE_USE.get(t["name"], ""), "rows": count, "columns": cols}
    return {"ok": True, "db": str(DB), "joins": "every table but flashes and commands: USING (run_id)",
            "tables": tables}


def list_runs(db, where=None, limit=20):
    sql = f"SELECT {RUN_LIST} FROM runs" + (f" WHERE {where}" if where else "") + " ORDER BY started DESC LIMIT ?"
    return {"ok": True, "runs": rows(db, sql, (int(limit),))}


def show(db, which):
    run = find_run(db, which)
    rid = run["run_id"]
    return {"ok": True, "run": run,
            "disarms": rows(db, "SELECT t_s, tick, reason, reason_text FROM disarms WHERE run_id = ? ORDER BY t_s", (rid,)),
            "steps": rows(db, "SELECT n, name, speed, turn, secs, got_speed, got_turn FROM steps WHERE run_id = ? ORDER BY n", (rid,)),
            "gains": rows(db, "SELECT * FROM gains WHERE run_id = ? ORDER BY ver", (rid,)),
            "commands": rows(db, "SELECT time, cmd, speed, turn, secs, moved_turns, settled, ok FROM commands "
                                 "WHERE run_id = ? ORDER BY time", (rid,)),
            "header": [f"{m['name']},{m['value']}" for m in
                       rows(db, "SELECT name, value FROM meta WHERE run_id = ? ORDER BY line", (rid,))],
            "tick_columns": tick_columns(db)}


def tick_columns(db):
    return [c["name"] for c in rows(db, "SELECT name FROM pragma_table_info('ticks')") if c["name"] != "run_id"]


def ticks(db, which, cols=None, start=None, end=None, every=None, limit=MAX_ROWS):
    """A slice of one run's telemetry. cols: names from tick_columns (default a useful few);
    start/end: seconds into the file; every: keep every Nth row (default: enough to fit limit)."""
    run = find_run(db, which)
    allowed = tick_columns(db)
    cols = cols or ["t_s", "mode", "pitch", "setpoint", "speed_tps", "turn_tps", "pwm_l", "pwm_r",
                    "drive_speed", "drive_turn"]
    bad = [c for c in cols if c not in allowed]
    if bad:
        raise Refused(f"unknown column(s) {bad}: ticks has {allowed}")
    if "t_s" not in cols:
        cols = ["t_s"] + cols
    where, params = "run_id = ?", [run["run_id"]]
    if start is not None:
        where += " AND t_s >= ?"; params.append(float(start))
    if end is not None:
        where += " AND t_s <= ?"; params.append(float(end))
    total = db.execute(f"SELECT count(*) FROM ticks WHERE {where}", params).fetchone()[0]
    limit = max(1, int(limit))
    every = max(1, int(every or -(-total // limit)))                   # ceiling: fit in limit
    sql = (f"SELECT {', '.join(cols)} FROM (SELECT *, row_number() OVER (ORDER BY rowid) - 1 AS k "
           f"FROM ticks WHERE {where}) WHERE k % ? = 0 LIMIT ?")
    data = db.execute(sql, params + [every, limit]).fetchall()
    return {"ok": True, "file": run["file"], "run_id": run["run_id"], "rows_total": total, "every": every,
            "returned": len(data), "columns": cols, "rows": [list(r) for r in data]}


def csv_text(answer):
    out = io.StringIO()
    w = csv.writer(out)
    w.writerow(answer["columns"])
    w.writerows(answer["rows"])
    return out.getvalue()


def sql(db, query, limit=MAX_ROWS):
    """Any SELECT. The database is open read only, so even a bad query can't change anything."""
    if not query.lstrip().upper().startswith(("SELECT", "WITH")):
        raise Refused("only SELECT (or WITH ... SELECT) queries")
    try:
        cur = db.execute(query)
    except (sqlite3.Error, sqlite3.Warning) as e:   # Warning: more than one statement
        raise Refused(f"SQL: {e}")
    names = [d[0] for d in cur.description]
    data = cur.fetchmany(int(limit) + 1)
    return {"ok": True, "columns": names, "rows": [list(r) for r in data[:int(limit)]],
            "truncated": len(data) > int(limit)}


# --- the web page: the same answers over HTTP, on this computer only -------------------------

class Handler(BaseHTTPRequestHandler):
    db = None

    def send(self, code, body, kind="application/json", filename=None):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", kind)
        if filename:
            self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = {k: v[-1] for k, v in urllib.parse.parse_qs(url.query).items()}
        parts = [urllib.parse.unquote(p) for p in url.path.strip("/").split("/") if p]
        try:
            if not parts:
                return self.send(200, PAGE.read_text(), "text/html; charset=utf-8")
            if parts[0] != "api":
                return self.send(404, json.dumps({"ok": False, "error": "not found"}))
            route = parts[1:]
            if route == ["schema"]:
                answer = schema(self.db)
            elif route == ["runs"]:
                answer = list_runs(self.db, q.get("where"), q.get("limit", 200))
            elif route == ["sql"]:
                answer = sql(self.db, q.get("q", ""), q.get("limit", MAX_ROWS))
            elif len(route) == 2 and route[0] == "runs":
                answer = show(self.db, route[1])
            elif len(route) == 3 and route[0] == "runs" and route[2] == "ticks":
                answer = ticks(self.db, route[1], q["cols"].split(",") if q.get("cols") else None,
                               q.get("from"), q.get("to"), q.get("every"), q.get("limit", MAX_ROWS))
                if q.get("format") == "csv":
                    return self.send(200, csv_text(answer), "text/csv", f"{Path(answer['file']).stem}_slice.csv")
            elif len(route) == 3 and route[0] == "runs" and route[2] == "csv":
                run = find_run(self.db, route[1])
                return self.send(200, (LOGS / run["file"]).read_bytes(), "text/csv", run["file"])
            else:
                return self.send(404, json.dumps({"ok": False, "error": f"no route {url.path}"}))
            self.send(200, json.dumps(answer))
        except Refused as e:
            self.send(400, json.dumps({"ok": False, "error": str(e)}))
        except (sqlite3.Error, sqlite3.Warning) as e:   # Warning: more than one statement
            self.send(400, json.dumps({"ok": False, "error": f"SQL: {e}"}))

    def log_message(self, *args):                       # quiet: one line per request is noise here
        pass


def serve(port):
    Handler.db = open_db()
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)      # this computer only
    print(f"Logs page: http://localhost:{port}   (JSON: /api/schema, /api/runs, /api/runs/<run>, "
          f"/api/runs/<run>/ticks, /api/runs/<run>/csv, /api/sql?q=...)  Ctrl-C stops.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", help=f"catalog to read (default {DB.relative_to(REPO)}); its logs are the CSVs next to it")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("schema", help="every table, what it's for, its columns and row count")
    p = sub.add_parser("runs", help="list runs, newest first")
    p.add_argument("--where", help="SQL condition on runs, e.g. \"surface = 'carpet' AND armed_s > 60\"")
    p.add_argument("--limit", type=int, default=20)
    p = sub.add_parser("show", help="one run: summary, disarms, steps, gains, commands, header")
    p.add_argument("run")
    for name in ("ticks", "download"):
        p = sub.add_parser(name, help="a slice of one run's telemetry" if name == "ticks"
                           else "save a run's original CSV (or with --cols, a slice)")
        p.add_argument("run")
        p.add_argument("--cols", help="comma-separated tick columns (see 'show' tick_columns)")
        p.add_argument("--from", dest="start", type=float, help="seconds into the file")
        p.add_argument("--to", dest="end", type=float, help="seconds into the file")
        p.add_argument("--every", type=int, help="keep every Nth row")
        p.add_argument("--limit", type=int, default=MAX_ROWS if name == "ticks" else 10**9)
        if name == "download":
            p.add_argument("--out", help="file to write (default: the log's name, in this folder)")
    p = sub.add_parser("sql", help="any read-only SELECT")
    p.add_argument("query")
    p.add_argument("--limit", type=int, default=MAX_ROWS)
    p = sub.add_parser("serve", help="the web page")
    p.add_argument("--port", type=int, default=8090)
    args = ap.parse_args()
    if args.db:
        use_db(args.db)

    try:
        if args.cmd == "serve":
            return serve(args.port)
        db = open_db()
        if args.cmd == "schema":
            answer = schema(db)
        elif args.cmd == "runs":
            answer = list_runs(db, args.where, args.limit)
        elif args.cmd == "show":
            answer = show(db, args.run)
        elif args.cmd == "ticks":
            answer = ticks(db, args.run, args.cols and args.cols.split(","), args.start, args.end, args.every, args.limit)
        elif args.cmd == "download":
            run = find_run(db, args.run)
            sliced = args.cols or args.start is not None or args.end is not None or args.every
            out = Path(args.out or (f"{Path(run['file']).stem}_slice.csv" if sliced else run["file"]))
            if sliced:
                cut = ticks(db, args.run, args.cols and args.cols.split(","), args.start, args.end, args.every, args.limit)
                out.write_text(csv_text(cut))
                answer = {"ok": True, "file": str(out), "rows": cut["returned"], "columns": cut["columns"]}
            else:
                shutil.copyfile(LOGS / run["file"], out)       # the whole log, header lines and all
                answer = {"ok": True, "file": str(out), "rows": run["rows"], "source": run["file"]}
        else:
            answer = sql(db, args.query, args.limit)
    except Refused as e:
        print(json.dumps({"ok": False, "error": str(e)}))
        sys.exit(1)
    except (sqlite3.Error, sqlite3.Warning) as e:   # Warning: more than one statement
        print(json.dumps({"ok": False, "error": f"SQL: {e}"}))
        sys.exit(1)
    print(json.dumps(answer, default=str))


if __name__ == "__main__":
    main()
