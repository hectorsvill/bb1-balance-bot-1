"""bb1.py - control BB1 from the command line. Every answer is one line of JSON.

Made for scripts and AI agents (milestone 3), and handy by hand too.

Usage (BB1 on Wi-Fi; set BB1_HOST once to skip --host):
    tools/venv/bin/python tools/bb1.py api                         # firmware version, limits, every endpoint
    tools/venv/bin/python tools/bb1.py state                       # short summary
    tools/venv/bin/python tools/bb1.py state --full                # everything /state says
    tools/venv/bin/python tools/bb1.py arm                         # start balancing (stand it up first)
    tools/venv/bin/python tools/bb1.py drive --speed 0.3 --for 2   # forward 0.3 wheel turns/s for 2 s
    tools/venv/bin/python tools/bb1.py drive --turn -0.4 --for 1   # turn left on the spot for 1 s
    tools/venv/bin/python tools/bb1.py drive --speed 0.3 --for 5 --max 0.3 --max-turn 0.4 --max-for 3   # an owner's caps
    tools/venv/bin/python tools/bb1.py spin --deg 90                # turn 90 deg right on the spot, counted by BB1's gyro
    tools/venv/bin/python tools/bb1.py spin --deg -360 --max-turn 0.4 --max-deg 360   # a full turn left, an owner's caps
    tools/venv/bin/python tools/bb1.py stop                        # stop driving, keep balancing
    tools/venv/bin/python tools/bb1.py disarm                      # motors off: BB1 falls over!
    tools/venv/bin/python tools/bb1.py log status                  # is BB1's telemetry being recorded?
    tools/venv/bin/python tools/bb1.py log start --surface wood -n "square"   # label the log (starts it if needed)
    tools/venv/bin/python tools/bb1.py log stop                    # save the log now

An agent's instructions for using this tool are in docs/AGENT.md. The MCP
server (tools/bb1_mcp.py) calls run() below, so it gets every check here too.

Units are the firmware's (docs/API.md): speed in wheel turns/s (forward +),
turn in turns/s of left-minus-right wheel difference (right +). One wheel
turn is 0.178 m (measured).

Safety (an agent can make mistakes, so the tool checks for it):
  - drive refuses unless /state says armed with no encoder fault
  - speed is capped at --max (default 0.5, the firmware allows 3.0), turn at
    --max-turn (default: the same as --max)
  - --for is at most --max-for, which can only lower MAX_FOR_S (10 s); each
    /drive command lasts CMD_MS (1 s)
    and is re-sent every 100 ms, so if this tool dies BB1 stops by itself
    within 1 s (the firmware failsafe)
  - it watches /state while driving: a fall, a disarm, an encoder fault or
    more than MAX_TRAVEL wheel turns from the start sends /drive/stop and ends it.
    MAX_TRAVEL is a distance (the room), not a time, so it stays 3 turns at
    any speed: at 3 turns/s a drive ends after about 1 s, and BB1 rolls on
    up to about 3 more turns (0.5 s between /state checks + a 1 s stop)
  - Ctrl-C sends /drive/stop
  - spin (firmware 3.11, POST /spin) has the same checks: armed, no encoder
    fault, the rate capped at --max-turn (default 0.5), the angle at --max-deg
    (can only lower MAX_SPIN_DEG, 360). BB1 lands on the angle by itself with
    its gyro; this tool waits for /state to say it has, watching for a fall,
    a disarm or an encoder fault like drive does, and stops it if it hasn't
    landed in time. The answer's turned_deg is the gyro's own count

Exit codes: 0 done, 1 BB1 refused or a safety check ended it, 2 BB1 didn't answer
or the arguments were wrong. Ctrl-C is 1 here (this tool's contract is only
0, 1 and 2). Output is always one JSON line. --json is accepted so every
tool takes the same flag; it does not change the output.

Every call against the real robot adds one row to logs/agent_commands.csv
(time, host, cmd, args, exit_code, answer), so an agent's session leaves a
trace that tools/runs_db.py can import next to the logs. Calls to the mock
(localhost, 127.*) are not recorded: mock data never mixes with real data.

BB1's own telemetry is recorded automatically too: once BB1 is balancing
after an `arm` or a `drive`, a background recorder (tools/session_log.py)
reads every 5 ms record and, when the session ends, saves the log to logs/
and logs/runs.sqlite, where the commands above are linked to it. `log`
shows, labels or stops it.
"""

import argparse
import csv
import datetime
import json
import os
import socket
import sys
import time
import urllib.error

from pathlib import Path

import cli_util
import session_log
from logger import get, post

COUNTS_PER_REV = 520     # config.h, assumed
CMD_MS = 1000            # each /drive command lasts this long (the Pico's server sometimes goes quiet 1-2.5 s)
RESEND_S = 0.1           # re-send the command this often, like the page
MAX_FOR_S = 10.0         # longest single drive
MAX_TRAVEL = 3.0         # wheel turns (about 0.53 m) from where the drive started: stop
STALL_S = 2.0            # no answer at all from BB1 for this long: stop and give up
SETTLE_S = 2.0           # after a drive, wait up to this long for the wheels to stop before answering
MAX_SPIN_DEG = 360.0     # biggest single spin this tool sends (the firmware allows 1080)
SPIN_RATE = 2.0          # default spin rate, turns/s of wheel difference (then capped at --max-turn)
SPIN_MIN_RATE = 0.3      # the firmware's lowest spin rate (config.h SPIN_MIN_RATE, firmware 3.11)
SETTLED_SPEED = 0.05     # wheel turns/s: slower than this counts as stopped
ARM_WAIT_S = 1.5         # how long to wait for an arm to be accepted or refused
COMMAND_LOG = Path(__file__).resolve().parent.parent / "logs" / "agent_commands.csv"
COMMAND_LOG_HEADER = ["time", "host", "cmd", "args", "exit_code", "answer"]
printed_line = ""        # the JSON line out() made last: it goes into the command log
answer = {}              # the same answer as a dict, for run() (the MCP server)
echo = True              # print answers? run() can turn it off: on MCP's stdio, stdout is the protocol


class NoAnswer(Exception):
    """BB1 didn't answer."""


def out(**fields):
    """Print one JSON line: the tool's whole answer."""
    global printed_line, answer
    answer = fields
    printed_line = json.dumps(fields)
    if echo:
        print(printed_line, flush=True)


def is_mock(host):
    name = host.split("://")[-1].partition(":")[0]
    return name == "localhost" or name.startswith("127.")


def record(args, exit_code):
    """One row in logs/agent_commands.csv for this call. Never breaks the
    command: if the file can't be written, say so on stderr and carry on."""
    if is_mock(args.host):
        return
    try:
        new = not COMMAND_LOG.exists()
        with open(COMMAND_LOG, "a", newline="") as f:
            w = csv.writer(f)
            if new:
                w.writerow(COMMAND_LOG_HEADER)
            options = {k: v for k, v in vars(args).items() if k not in ("cmd", "host", "via")}
            # cmd says who asked: "drive" from the command line, "mcp:drive" from tools/bb1_mcp.py
            w.writerow([datetime.datetime.now().astimezone().isoformat(timespec="milliseconds"),
                        args.host, f"{args.via}:{args.cmd}" if args.via else args.cmd,
                        json.dumps(options), exit_code, printed_line])
    except OSError as e:
        print(f"bb1.py: couldn't write {COMMAND_LOG}: {e}", file=sys.stderr)


def resolve(host):
    """Look the name up once: .local lookups can be slow, and every call pays it."""
    name, _, port = host.partition(":")
    try:
        ip = socket.gethostbyname(name)
    except OSError:
        raise NoAnswer(f"can't find {name}: try --host <ip> ('wifi' on serial shows it)")
    return f"http://{ip}{':' + port if port else ''}"


def read_state(base):
    try:
        return json.loads(get(f"{base}/state", timeout=3))
    except (OSError, ValueError) as e:      # OSError covers timeouts and refused connections
        raise NoAnswer(f"/state: {e}")


def turns(s):
    """Wheel position, turns from boot (average of both wheels, forward +)."""
    return (s["enc_l"] + s["enc_r"]) / 2 / COUNTS_PER_REV


def diff(s):
    """Left-minus-right wheel turns from boot (right turn +): how far it has turned.
    Not halved: the same measure as the `turn` command (drive.h) and drive_test.py."""
    return (s["enc_l"] - s["enc_r"]) / COUNTS_PER_REV


def wheel_speed(s):
    return (s["speed_l"] + s["speed_r"]) / 2 / COUNTS_PER_REV


def summary(s):
    """The fields an agent needs to decide what to do next, in plain units."""
    cells = s.get("batt_cells", 2) or 2
    return {
        "armed": s["armed"],
        "reason": s["reason"],                    # why it last stopped or refused ("" if none)
        "auto_state": s["auto_state"],
        "pitch_deg": s["pitch"],
        "speed": round(wheel_speed(s), 2),            # wheel turns/s now
        "position_turns": round(turns(s), 2),     # since boot; differences matter, not the value
        "drive": {k: s["drive"][k] for k in ("speed", "turn", "left_ms")},
        "enc_fault": s["enc_fault"],
        "trim_learned_deg": s.get("trim_learned"),   # firmware 3.5 on; None before
        "yaw_deg": s.get("yaw_deg"),              # gyro heading since power-on, right + (firmware 3.11 on; None before)
        "batt_v_per_cell": round(s["batt_v"] / cells, 2) if s.get("batt_ok") else None,
        "ip": s.get("ip"),
        # Front distance sensor (VL53L0X, not fitted yet): passed through as soon as the
        # firmware's /state has it, so agents see it without a tool change. Absent until then.
        **({"distance_mm": s["distance_mm"]} if "distance_mm" in s else {}),
    }


def cmd_api(base, args):
    # GET /api (api.h): an agent checks "api" against the version docs/AGENT.md was written for.
    try:
        api = json.loads(get(f"{base}/api", timeout=5))
    except urllib.error.HTTPError:          # firmware older than GET /api (before 3.1)
        out(ok=False, error="this firmware has no /api: docs/AGENT.md doesn't match it")
        return 1
    except (OSError, ValueError) as e:
        raise NoAnswer(f"/api: {e}")
    out(ok=True, **api)
    return 0


def cmd_state(base, args):
    s = read_state(base)
    out(ok=True, state=s if args.full else summary(s))
    return 0


def cmd_arm(base, args):
    before = read_state(base)
    if before["armed"]:
        out(ok=True, armed=True, note="already balancing")
        return 0
    # heartbeat=0: keep balancing without /state polls (an agent may go quiet
    # for a while). An arm also turns auto-arm back on, even when refused.
    post(f"{base}/arm", seconds=args.seconds, heartbeat=0)
    end = time.time() + ARM_WAIT_S
    while time.time() < end:                    # core 1 decides between ticks: wait for the answer
        time.sleep(0.1)
        s = read_state(base)
        if s["arm_attempts"] != before["arm_attempts"]:
            break
    out(ok=s["armed"], armed=s["armed"], reason=s["reason"], auto_state=s["auto_state"])
    return 0 if s["armed"] else 1


def cmd_disarm(base, args):
    post(f"{base}/disarm")
    out(ok=True, armed=False, note="motors off: auto-arm stays off until the next arm")
    return 0


def cmd_stop(base, args):
    post(f"{base}/drive/stop")
    out(ok=True, stopped=True)
    return 0


def cmd_drive(base, args):
    max_turn = args.max if args.max_turn is None else args.max_turn
    speed = max(-args.max, min(args.max, args.speed))   # clamp, like the firmware does
    turn = max(-max_turn, min(max_turn, args.turn))
    secs = max(0.0, min(MAX_FOR_S, args.max_for, args.secs))   # --max-for can lower the 10 s, never raise it
    if speed == 0 and turn == 0:                # nothing to do: say so instead of standing still for secs
        out(ok=False, error="nothing to drive: give --speed and/or --turn (not both 0)")
        return 1
    # Say what was cut, so an agent doesn't think it got what it asked for
    cut = [f"{name} {asked:g} cut to {got:g}" for name, asked, got in
           (("--speed", args.speed, speed), ("--turn", args.turn, turn), ("--for", args.secs, secs)) if asked != got]
    s = read_state(base)
    if not s["armed"]:
        out(ok=False, error="not balancing: run 'arm' first", reason=s["reason"])
        return 1
    if s["enc_fault"]:
        out(ok=False, error=f"encoder fault {s['enc_fault']}: check that encoder's wires")
        return 1

    start, start_diff, start_yaw = turns(s), diff(s), s.get("yaw_deg")
    end = time.time() + secs
    next_check = 0.0
    last_answer = time.time()
    why = None                                  # set when a safety check ends the drive early
    try:
        while time.time() < end:
            try:
                post(f"{base}/drive", speed=f"{speed:.2f}", turn=f"{turn:.2f}", ms=CMD_MS)
                last_answer = time.time()
            except urllib.error.HTTPError as e:     # 409: not balancing any more, or encoder fault
                why = json.loads(e.read().decode() or "{}").get("error", f"HTTP {e.code}")
                break
            except OSError:                         # a Wi-Fi stall: BB1's own failsafe covers it
                if time.time() - last_answer > STALL_S:
                    raise NoAnswer("no answer while driving")
            if time.time() >= next_check:           # /state twice a second is enough to catch a fall
                next_check = time.time() + 0.5
                s = read_state(base)
                if not s["armed"]:
                    why = f"disarmed: {s['reason']}"
                    break
                if s["enc_fault"]:
                    why = f"encoder fault {s['enc_fault']}"
                    break
                if abs(turns(s) - start) > MAX_TRAVEL:
                    why = f"travelled more than {MAX_TRAVEL:g} wheel turns"
                    break
            time.sleep(RESEND_S)
    finally:                                    # always: on the end, a safety stop, an error or Ctrl-C
        try:
            post(f"{base}/drive/stop")
        except OSError:
            pass                                # the command runs out by itself within CMD_MS
    # BB1 ramps down and rolls on a little after a stop (up to about 0.5 turn):
    # wait for it, so moved/turned are where it really ended up.
    settle_end = time.time() + SETTLE_S
    time.sleep(0.3)                             # the speed readout lags about 0.1 s
    s = read_state(base)
    while abs(wheel_speed(s)) > SETTLED_SPEED and time.time() < settle_end:
        time.sleep(0.1)
        s = read_state(base)
    out(ok=why is None, error=why, asked={"speed": speed, "turn": turn, "secs": secs}, cut=cut,
        moved_turns=round(turns(s) - start, 2), turned_turns=round(diff(s) - start_diff, 2),
        turned_deg=yaw_change(start_yaw, s),
        settled=abs(wheel_speed(s)) <= SETTLED_SPEED, state=summary(s))
    return 0 if why is None else 1


def yaw_change(start_yaw, s):
    """Degrees the gyro says BB1 turned (right +), or None on firmware before 3.11."""
    if start_yaw is None or s.get("yaw_deg") is None:
        return None
    return round(s["yaw_deg"] - start_yaw, 1)


def cmd_spin(base, args):
    """Turn on the spot by an angle (POST /spin). BB1 counts it with its gyro and
    lands on it by itself; this waits for that, with the same checks as drive."""
    max_turn = args.max_turn
    max_deg = min(MAX_SPIN_DEG, args.max_deg)            # --max-deg can lower 360, never raise it
    deg = max(-max_deg, min(max_deg, args.deg))
    rate = min(abs(args.rate) if args.rate is not None else SPIN_RATE, max_turn)
    cut = [f"{name} {asked:g} cut to {got:g}" for name, asked, got in
           (("--deg", args.deg, deg), ("--rate", args.rate, rate)) if asked is not None and asked != got]   # the default rate isn't "asked"
    if abs(deg) < 5:
        out(ok=False, error="spin at least 5 deg either way (right +, left -)")
        return 1
    if rate < SPIN_MIN_RATE:
        out(ok=False, error=f"--max-turn {max_turn:g} is below BB1's slowest spin ({SPIN_MIN_RATE:g}): use drive --turn instead")
        return 1
    s = read_state(base)
    if not s["armed"]:
        out(ok=False, error="not balancing: run 'arm' first", reason=s["reason"])
        return 1
    if s["enc_fault"]:
        out(ok=False, error=f"encoder fault {s['enc_fault']}: check that encoder's wires")
        return 1
    if "yaw_deg" not in s:
        out(ok=False, error="this firmware has no exact-angle spins (needs 3.11): use drive --turn")
        return 1
    start, start_diff, start_yaw = turns(s), diff(s), s["yaw_deg"]
    try:
        answer_json = json.loads(post(f"{base}/spin", deg=f"{deg:.1f}", rate=f"{rate:.2f}"))
    except urllib.error.HTTPError as e:                  # 409 / 400: BB1 says why
        out(ok=False, error=json.loads(e.read().decode() or "{}").get("error", f"HTTP {e.code}"))
        return 1
    deadline = time.time() + answer_json.get("timeout_ms", 10000) / 1000 + 1
    began = time.time()
    why, landed = None, False
    try:
        while time.time() < deadline:
            time.sleep(0.2)
            s = read_state(base)
            if not s["armed"]:
                why = f"disarmed: {s['reason']}"
                break
            if s["enc_fault"]:
                why = f"encoder fault {s['enc_fault']}"
                break
            if abs(turns(s) - start) > MAX_TRAVEL:
                why = f"travelled more than {MAX_TRAVEL:g} wheel turns"
                break
            if time.time() - began > 0.4 and not s["drive"].get("spin_left"):
                landed = True                            # BB1 says it's there (and has settled)
                break
        else:
            why = "didn't land in time"
    finally:
        if not landed:                                   # a safety stop, a timeout, an error or Ctrl-C
            try:
                post(f"{base}/drive/stop")
            except OSError:
                pass
    time.sleep(0.3)
    s = read_state(base)
    turned = yaw_change(start_yaw, s)
    out(ok=why is None, error=why, asked={"deg": deg, "rate": rate}, cut=cut, landed=landed,
        turned_deg=turned, error_deg=round(turned - deg, 1) if turned is not None else None,
        turned_turns=round(diff(s) - start_diff, 2), moved_turns=round(turns(s) - start, 2), state=summary(s))
    return 0 if why is None else 1


def parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("BB1_HOST", "bb1.local"),
                    help="robot address, optionally with :port (default $BB1_HOST or bb1.local)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("api", help="firmware version, limits and every endpoint (run first)")
    p = sub.add_parser("state", help="what BB1 is doing now")
    p.add_argument("--full", action="store_true", help="every /state field, not the summary")
    p = sub.add_parser("arm", help="start balancing (it must be standing near upright)")
    p.add_argument("--seconds", type=int, default=0, help="disarm after this long (0 = no limit)")
    sub.add_parser("disarm", help="motors off (BB1 falls over)")
    sub.add_parser("stop", help="stop driving, keep balancing")
    p = sub.add_parser("drive", help="drive for a while, then stop")
    p.add_argument("--speed", type=float, default=0.0, help="wheel turns/s, forward +")
    p.add_argument("--turn", type=float, default=0.0, help="turns/s of left-minus-right, right +")
    p.add_argument("--for", dest="secs", type=float, default=1.0, help=f"seconds (max {MAX_FOR_S:g})")
    p.add_argument("--max", type=float, default=0.5, help="cap on speed (and turn, unless --max-turn) (default 0.5)")
    p.add_argument("--max-turn", type=float, help="cap on turn (default: --max)")
    p.add_argument("--max-for", type=float, default=MAX_FOR_S, help=f"cap on --for; can only lower {MAX_FOR_S:g} s")
    p = sub.add_parser("spin", help="turn on the spot by an angle; BB1 lands on it with its gyro (firmware 3.11)")
    p.add_argument("--deg", type=float, required=True, help=f"degrees, right + (max {MAX_SPIN_DEG:g})")
    p.add_argument("--rate", type=float, help=f"turns/s of left-minus-right (default {SPIN_RATE:g}, then capped at --max-turn)")
    p.add_argument("--max-turn", type=float, default=0.5, help="cap on the rate (default 0.5)")
    p.add_argument("--max-deg", type=float, default=MAX_SPIN_DEG, help=f"cap on --deg; can only lower {MAX_SPIN_DEG:g}")
    p = sub.add_parser("log", help="BB1's telemetry recording (automatic): status, start (label it), stop (save now)")
    p.add_argument("action", choices=("status", "start", "stop"))
    p.add_argument("-n", "--note", help="start: a note saved in the log and its file name")
    p.add_argument("--surface", help="start: what BB1 stands on: wood, carpet... (default $BB1_SURFACE or unknown)")
    cli_util.standard_cli(ap, always_json=True)
    return ap


def cmd_log(host, args):
    """The telemetry recorder (session_log.py). Takes the host name, not an
    address: status and stop work even when BB1 is off."""
    if is_mock(host):
        out(ok=False, error="the mock is never recorded (mock data never mixes with real data)")
        return 1
    if args.action == "status":
        out(ok=True, **session_log.status())
    elif args.action == "start":
        label = session_log.set_label(args.note, args.surface)
        pid, new = session_log.ensure(resolve(host), host)
        out(ok=True, recording=True, started_now=new, label=label,
            note="saved when you run 'log stop' or the session ends (see 'log status')")
    else:
        saved = session_log.stop()
        out(ok=True, was_recording=saved is not None, saved=saved)
    return 0


COMMANDS = {"api": cmd_api, "state": cmd_state, "arm": cmd_arm, "disarm": cmd_disarm, "stop": cmd_stop, "drive": cmd_drive,
            "spin": cmd_spin}


def run(argv, print_answer=True, via=None):
    """Run one command from its argument list, exactly as the command line would.
    Returns (exit code, answer dict). The MCP server calls this with
    print_answer=False and via="mcp": the same parsing, caps, safety checks and
    command log as the command line, with nothing printed."""
    global echo
    echo = print_answer
    args = parser().parse_args(argv)
    args.via = via
    base = None
    try:
        if args.cmd == "log":
            code = cmd_log(args.host, args)
        else:
            base = resolve(args.host)
            code = COMMANDS[args.cmd](base, args)
    except NoAnswer as e:
        out(ok=False, error=f"BB1 didn't answer: {e}")
        code = 2
    except KeyboardInterrupt:
        out(ok=False, error="stopped by Ctrl-C")
        code = 1
    except OSError as e:                        # a POST other than /drive got no answer
        out(ok=False, error=f"BB1 didn't answer: {e}")
        code = 2
    record(args, code)                          # every call leaves a trace (real robot only)
    if base and not is_mock(args.host):
        auto_record(base, args)
    return code, answer


def auto_record(base, args):
    """Keep BB1's telemetry recording while an agent works (session_log.py).
    Started once BB1 is balancing after an arm or a drive; every command tells
    the recorder the agent is still busy. Never breaks the command."""
    try:
        session_log.touch_command()
        balancing = answer.get("armed") or (answer.get("state") or {}).get("armed")
        if args.cmd in ("arm", "drive", "spin") and balancing:
            session_log.ensure(base, args.host)
    except Exception as e:
        print(f"bb1.py: telemetry recorder: {e}", file=sys.stderr)


def main():
    code, _ = run(sys.argv[1:])
    sys.exit(code)


if __name__ == "__main__":
    main()
