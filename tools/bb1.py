"""bb1.py - control BB1 from the command line. Every answer is one line of JSON.

Made for scripts and AI agents (milestone 3), and handy by hand too.

Usage (BB1 on Wi-Fi; set BB1_HOST once to skip --host):
    tools/venv/bin/python tools/bb1.py state                       # short summary
    tools/venv/bin/python tools/bb1.py state --full                # everything /state says
    tools/venv/bin/python tools/bb1.py arm                         # start balancing (stand it up first)
    tools/venv/bin/python tools/bb1.py drive --speed 0.3 --for 2   # forward 0.3 wheel turns/s for 2 s
    tools/venv/bin/python tools/bb1.py drive --turn -0.4 --for 1   # turn left on the spot for 1 s
    tools/venv/bin/python tools/bb1.py stop                        # stop driving, keep balancing
    tools/venv/bin/python tools/bb1.py disarm                      # motors off: BB1 falls over!

Units are the firmware's (docs/API.md): speed in wheel turns/s (forward +),
turn in turns/s of left-minus-right wheel difference (right +). One wheel
turn is 0.178 m (measured).

Safety (an agent can make mistakes, so the tool checks for it):
  - drive refuses unless /state says armed with no encoder fault
  - speed and turn are capped at --max (default 0.5, the firmware allows 1.0)
  - --for is at most MAX_FOR_S (10 s); each /drive command lasts CMD_MS (1 s)
    and is re-sent every 100 ms, so if this tool dies BB1 stops by itself
    within 1 s (the firmware failsafe)
  - it watches /state while driving: a fall, a disarm, an encoder fault or
    more than MAX_TRAVEL wheel turns from the start sends /drive/stop and ends it
  - Ctrl-C sends /drive/stop

Exit codes: 0 done, 1 BB1 refused or a safety check ended it, 2 BB1 didn't answer.
"""

import argparse
import json
import os
import socket
import sys
import time
import urllib.error

from logger import get, post

COUNTS_PER_REV = 520     # config.h, assumed
CMD_MS = 1000            # each /drive command lasts this long (the Pico's server sometimes goes quiet 1-2.5 s)
RESEND_S = 0.1           # re-send the command this often, like the page
MAX_FOR_S = 10.0         # longest single drive
MAX_TRAVEL = 3.0         # wheel turns (about 0.53 m) from where the drive started: stop
STALL_S = 2.0            # no answer at all from BB1 for this long: stop and give up
SETTLE_S = 2.0           # after a drive, wait up to this long for the wheels to stop before answering
SETTLED_SPEED = 0.05     # wheel turns/s: slower than this counts as stopped
ARM_WAIT_S = 1.5         # how long to wait for an arm to be accepted or refused


class NoAnswer(Exception):
    """BB1 didn't answer."""


def out(**fields):
    """Print one JSON line: the tool's whole answer."""
    print(json.dumps(fields), flush=True)


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
    """Left-minus-right wheel difference, turns from boot (right turn +): how far it has turned."""
    return (s["enc_l"] - s["enc_r"]) / 2 / COUNTS_PER_REV


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
        "batt_v_per_cell": round(s["batt_v"] / cells, 2) if s.get("batt_ok") else None,
        "ip": s.get("ip"),
    }


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
    speed = max(-args.max, min(args.max, args.speed))   # clamp, like the firmware does
    turn = max(-args.max, min(args.max, args.turn))
    secs = max(0.0, min(MAX_FOR_S, args.secs))
    s = read_state(base)
    if not s["armed"]:
        out(ok=False, error="not balancing: run 'arm' first", reason=s["reason"])
        return 1
    if s["enc_fault"]:
        out(ok=False, error=f"encoder fault {s['enc_fault']}: check that encoder's wires")
        return 1

    start, start_diff = turns(s), diff(s)
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
    out(ok=why is None, error=why, asked={"speed": speed, "turn": turn, "secs": secs},
        moved_turns=round(turns(s) - start, 2), turned_turns=round(diff(s) - start_diff, 2),
        settled=abs(wheel_speed(s)) <= SETTLED_SPEED, state=summary(s))
    return 0 if why is None else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("BB1_HOST", "bb1.local"),
                    help="robot address, optionally with :port (default $BB1_HOST or bb1.local)")
    sub = ap.add_subparsers(dest="cmd", required=True)
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
    p.add_argument("--max", type=float, default=0.5, help="cap on speed and turn (default 0.5)")
    args = ap.parse_args()

    commands = {"state": cmd_state, "arm": cmd_arm, "disarm": cmd_disarm, "stop": cmd_stop, "drive": cmd_drive}
    try:
        sys.exit(commands[args.cmd](resolve(args.host), args))
    except NoAnswer as e:
        out(ok=False, error=f"BB1 didn't answer: {e}")
        sys.exit(2)
    except KeyboardInterrupt:
        out(ok=False, error="stopped by Ctrl-C")
        sys.exit(1)
    except OSError as e:                        # a POST other than /drive got no answer
        out(ok=False, error=f"BB1 didn't answer: {e}")
        sys.exit(2)


if __name__ == "__main__":
    main()
