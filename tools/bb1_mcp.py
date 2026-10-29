"""bb1_mcp.py - BB1 as MCP tools, so an AI agent (Claude Code, Claude Desktop, ...) can drive it.

Usage (Python 3.10 or newer: the mcp package needs it; see docs/TOOLS.md):
    claude mcp add bb1 -- "$PWD/tools/venv/bin/python" "$PWD/tools/bb1_mcp.py" --host 192.168.1.240
    tools/venv/bin/python tools/bb1_mcp.py --host localhost:8080          # against tools/mock_bb1.py
    tools/venv/bin/python tools/bb1_mcp.py --max-speed 0.3 --max-turn 0.4 --max-for 3   # the owner's caps

The agent client starts this as a subprocess and talks MCP over its stdin and
stdout (so nothing here may print to stdout). There is no robot logic in this
file: every robot tool calls bb1.run() with the same arguments the command line
would get, and every log tool calls the functions behind tools/logs.py. So each
safety check (armed, encoder fault, travel, stall, caps, the 1 s failsafe) and
the logs/agent_commands.csv trace live in one place, bb1.py, and an agent gets
exactly what docs/AGENT.md describes. Commands sent through here are recorded
with cmd "mcp:<command>" so we can tell them apart from the command line.

The caps (--max-speed, --max-turn, --max-for) are set by the owner when adding
the server, like the settings at the top of docs/AGENT.md: an agent can ask for
more, but bb1.py cuts it and the answer's "cut" list says so.
"""

import argparse
import os
import sqlite3
import sys
import threading
from typing import Any

try:
    from mcp.server.mcpserver import MCPServer
    from mcp.types import ToolAnnotations
except ImportError:                     # Python 3.9 (the Xcode one), or the package isn't installed
    sys.exit("bb1_mcp.py needs the 'mcp' package (Python 3.10+): see docs/TOOLS.md, bb1_mcp.py")

import bb1
import logs

TICKS_LIMIT = 500        # default rows per ticks answer: smaller than logs.py's 2000, an agent reads every row

INSTRUCTIONS = """BB1 is a small two-wheel self-balancing robot. Read docs/AGENT.md in the
bb1 repo before your first command: its settings and safety rules apply here too.
Start with bb1_api (check its "api" number against AGENT.md's version line), then
bb1_state. You can only drive while it is armed (balancing). Units: speed in wheel
turns/s (forward +), turn in turns/s of left-minus-right wheel difference (right +);
one wheel turn is 0.178 m. bb1_disarm makes BB1 fall over. Your owner must be
watching while you drive. The logs_* tools are read only."""

READ_ONLY = ToolAnnotations(read_only_hint=True, open_world_hint=False)
MOVES = ToolAnnotations(read_only_hint=False, destructive_hint=False)
FALLS = ToolAnnotations(read_only_hint=False, destructive_hint=True)    # disarm: BB1 tips over


def make_server(host, max_speed=0.3, max_turn=0.4, max_for=3.0, db=None):
    """The MCP server with every tool, talking to BB1 at host. The tests make
    one against tools/mock_bb1.py and a fixture catalog."""
    if db:
        logs.use_db(db)
    server = MCPServer("bb1", instructions=INSTRUCTIONS)
    # One robot command at a time: BB1 answers one request at a time, and bb1.py
    # keeps its answer in module globals. Tools may run on worker threads.
    lock = threading.Lock()

    def robot(*argv):
        with lock:
            try:
                code, answer = bb1.run(["--host", host, *argv], print_answer=False, via="mcp")
            except SystemExit as e:                     # argparse refused the arguments
                return {"ok": False, "exit_code": 1, "error": f"bad arguments for bb1.py {argv[0]} ({e})"}
        return {"exit_code": code, **answer}

    def catalog(ask):
        """Run one logs.py lookup on a fresh read-only connection (logs.py's own errors become ok: false)."""
        try:
            db_conn = logs.open_db()
            try:
                return ask(db_conn)
            finally:
                db_conn.close()
        except logs.Refused as e:
            return {"ok": False, "error": str(e)}
        except (sqlite3.Error, sqlite3.Warning) as e:
            return {"ok": False, "error": f"SQL: {e}"}

    # --- the robot (bb1.py) ---------------------------------------------------------------

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def bb1_api() -> dict[str, Any]:
        """Firmware version, units, limits and every HTTP endpoint (GET /api). Call it first and
        check "api" against the version line in docs/AGENT.md; if they differ, stop and tell your owner."""
        return robot("api")

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def bb1_state(full: bool = False) -> dict[str, Any]:
        """What BB1 is doing now: armed, reason (why it last stopped or refused), auto_state,
        pitch_deg, speed (wheel turns/s), position_turns (since power-on: subtract two readings),
        drive, enc_fault (0 = ok), batt_v_per_cell. full=true returns every /state field."""
        return robot("state", *(["--full"] if full else []))

    @server.tool(annotations=MOVES, structured_output=True)
    def bb1_arm(seconds: int = 0) -> dict[str, Any]:
        """Start balancing. Ask your owner first (AGENT.md may_arm); BB1 must be standing near
        upright, or it refuses and "reason" says why. seconds > 0 disarms by itself after that long."""
        return robot("arm", "--seconds", str(int(seconds)))

    # The description names this server's caps, so it is built here and passed in
    # (a docstring can't hold the numbers).
    @server.tool(annotations=MOVES, structured_output=True, description=(
        f"Drive while balancing, then stop and wait for BB1 to settle (blocks for secs plus up to 2 s). "
        f"speed: wheel turns/s, forward + (capped at {max_speed:g}). turn: turns/s of left-minus-right "
        f"wheel difference, right + (capped at {max_turn:g}). secs: at most {max_for:g}. Refused unless "
        f"armed with no encoder fault. Read 'cut' (what was capped), moved_turns, turned_turns, settled "
        f"and state. It stops by itself past 3 wheel turns from where the drive started."))
    def bb1_drive(speed: float = 0.0, turn: float = 0.0, secs: float = 1.0) -> dict[str, Any]:
        # The caps go to bb1.py, which clamps and reports what it cut, like on the command line
        return robot("drive", "--speed", repr(float(speed)), "--turn", repr(float(turn)),
                     "--for", repr(float(secs)), "--max", repr(max_speed), "--max-turn", repr(max_turn),
                     "--max-for", repr(max_for))

    @server.tool(annotations=MOVES, structured_output=True)
    def bb1_stop() -> dict[str, Any]:
        """Stop driving now and keep balancing (it ramps down and rolls on up to about 0.5 turn)."""
        return robot("stop")

    @server.tool(annotations=FALLS, structured_output=True)
    def bb1_disarm() -> dict[str, Any]:
        """Motors off: BB1 FALLS OVER. Only when your owner asks, or it is about to hit something
        and bb1_stop isn't enough. Auto-arm stays off until the next arm."""
        return robot("disarm")

    @server.tool(structured_output=True)
    def bb1_log(action: str = "status", note: str = "", surface: str = "") -> dict[str, Any]:
        """BB1's telemetry recording, which runs by itself once BB1 balances after bb1_arm or
        bb1_drive and is saved to logs/runs.sqlite when the session ends. action: "status"
        (recording? how much; last_saved has the run_id to read with logs_show), "start" (label
        the log with note and surface, e.g. "wood" or "carpet", and start it if needed), "stop"
        (save now). Moves nothing."""
        argv = ["log", action]
        if note:
            argv += ["--note", note]
        if surface:
            argv += ["--surface", surface]
        return robot(*argv)

    # --- the logs (logs.py, read only) ----------------------------------------------------

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def logs_schema() -> dict[str, Any]:
        """Every table in the run catalog (logs/runs.sqlite), what it is for, its columns and row count."""
        return catalog(logs.schema)

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def logs_runs(where: str = "", limit: int = 20) -> dict[str, Any]:
        """Recorded runs, newest first. where: an SQL condition on the runs table, e.g.
        "surface = 'carpet' AND armed_s > 60"."""
        return catalog(lambda db: logs.list_runs(db, where or None, limit))

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def logs_show(run: str) -> dict[str, Any]:
        """One run (its run_id, file name, or a unique part of the file name): summary, disarms,
        drive steps, gains, bb1.py commands, header lines and the tick columns you can ask for."""
        return catalog(lambda db: logs.show(db, run))

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def logs_ticks(run: str, cols: list[str] | None = None, start_s: float | None = None,
                   end_s: float | None = None, every: int | None = None, limit: int = TICKS_LIMIT) -> dict[str, Any]:
        """A slice of one run's 5 ms telemetry. cols: names from logs_show's tick_columns (default
        a useful few); start_s/end_s: seconds into the log; every: keep every Nth row. At most
        limit rows: "every" in the answer says how much it was thinned."""
        return catalog(lambda db: logs.ticks(db, run, cols, start_s, end_s, every, limit))

    @server.tool(annotations=READ_ONLY, structured_output=True)
    def logs_sql(query: str, limit: int = 200) -> dict[str, Any]:
        """One read-only SELECT (or WITH ... SELECT) on the run catalog; see logs_schema for tables."""
        return catalog(lambda db: logs.sql(db, query, limit))

    return server


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("BB1_HOST", "bb1.local"),
                    help="robot address, optionally with :port (default $BB1_HOST or bb1.local)")
    # Defaults are docs/AGENT.md's settings: the careful choice if the owner doesn't say
    ap.add_argument("--max-speed", type=float, default=0.3, help="cap on drive speed, wheel turns/s (default 0.3)")
    ap.add_argument("--max-turn", type=float, default=0.4, help="cap on drive turn, turns/s (default 0.4)")
    ap.add_argument("--max-for", type=float, default=3.0, help="longest single drive, s (default 3, bb1.py allows 10)")
    ap.add_argument("--db", help="run catalog for the logs_* tools (default logs/runs.sqlite)")
    args = ap.parse_args()
    make_server(args.host, args.max_speed, args.max_turn, args.max_for, args.db).run("stdio")


if __name__ == "__main__":
    main()
