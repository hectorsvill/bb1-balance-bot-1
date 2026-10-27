"""drive_test.py - drive BB1 through a named test over Wi-Fi, record it, report it.

Usage (BB1 must already be balancing on the floor):
    tools/venv/bin/python tools/drive_test.py stand            # 30 s standing still
    tools/venv/bin/python tools/drive_test.py fwd-back-30 -n "dff 60"
    tools/venv/bin/python tools/drive_test.py --list           # what each test does
    tools/venv/bin/python tools/drive_test.py --report logs/bb1_..._drive-test.csv   # re-read a saved run

Tests: stand, fwd-back-30, fwd-back-50, turn, arc, all (the five in a row).

Safety, in this order of importance:
  - refuses to start unless /state says armed and enc_fault 0
  - every /drive command lasts CMD_MS (1 s) and is re-sent every 100 ms,
    like the page; nothing longer than MAX_CMD_MS (1 s) is ever sent. If this
    script dies, BB1 stops by itself within 1 s (the firmware failsafe)
  - POST /drive/stop between steps, and on any error, fall or Ctrl-C
  - checks /state twice a second: disarmed (fell, STOP), encoder fault, or more than
    MAX_TRAVEL (3) wheel turns from the start ends the test

The log is the same file logger.py writes (logs/bb1_<date>_<time>_<note>.csv),
plus one "# step,name,speed,turn,start_tick,end_tick" line per step so the
report (and a model trained later) knows what was asked when.
"""

import argparse
import json
import socket
import sys
import time
import urllib.error
from datetime import datetime

import numpy as np

from check_dump import find_file, load_dump
from logger import MAX_PER_REQUEST, fetch_log, gains_versions, get, log_path, post, save_log

COUNTS_PER_REV = 520           # config.h, assumed
CMD_MS = 1000                  # how long each /drive command lasts. The Pico's server sometimes goes quiet
                               # for 1-2.5 s (2026-10-26): at 300 and 500 ms the command ran out mid-step
MAX_CMD_MS = 1000              # hard limit: never ask for more than this
RESEND_S = 0.1                 # re-send the command this often (the page does too)
FLAG_SAT, FLAG_STOPPING, FLAG_ENC_FAULT = 8, 16, 32   # telemetry.h
MAX_TRAVEL = 3.0               # wheel turns (about 0.53 m) from where the test started: stop the test.
                               # Without it, fwd-back runs crept back 1 turn each and BB1 hit a wall (2026-10-26)
STALL_S = 2.0                  # give up after this long with no answer at all from BB1

# A step is (name, speed turns/s, turn turns/s, seconds). Speed and turn 0 =
# no command: BB1 just balances (a pause to see how it stops).
P = ("pause", 0.0, 0.0, 3.0)
TESTS = {
    "stand":       [("stand", 0.0, 0.0, 30.0)],
    "fwd-back-30": [P, ("fwd 0.3", 0.3, 0.0, 3.0), P, ("back 0.3", -0.3, 0.0, 3.0), P],
    "fwd-back-50": [P, ("fwd 0.5", 0.5, 0.0, 3.0), P, ("back 0.5", -0.5, 0.0, 3.0), P],
    "turn":        [P, ("left 0.5", 0.0, -0.5, 3.0), P, ("right 0.5", 0.0, 0.5, 3.0), P],
    "arc":         [P, ("fwd 0.3 right 0.3", 0.3, 0.3, 3.0), P, ("fwd 0.3 left 0.3", 0.3, -0.3, 3.0), P],
}
TESTS["all"] = sum((TESTS[k] for k in ("stand", "fwd-back-30", "fwd-back-50", "turn", "arc")), [])


class Stop(Exception):
    """The test has to end now (fell, STOP pressed, encoder fault, no answer)."""


class Run:
    """Talks to BB1: records the log, sends drive commands, watches /state."""

    def __init__(self, base):
        self.base = base
        self.rows, self.lost, self.header, self.versions = [], 0, "", {}
        self.next_tick = None
        self.last_fetch = self.last_check = 0.0
        self.last_answer = time.time()  # when BB1 last answered anything
        self.tick = 0                   # newest tick seen in /state
        self.home = None                # wheel position (turns) when the test started
        self.travel = 0.0               # turns from home, forward +

    def ask(self, fn, *args, **kw):
        """Call BB1. Short Wi-Fi stalls (the Pico's server goes quiet for a second or
        two now and then) are ridden out: returns None and we try again next loop.
        BB1 is safe meanwhile, since each drive command runs out after CMD_MS.
        A refusal (HTTP 409 etc.) is a real answer and goes straight up."""
        try:
            out = fn(*args, **kw)
        except urllib.error.HTTPError:
            raise
        except OSError as e:          # timeout, connection reset
            if time.time() - self.last_answer > STALL_S:
                raise Stop(f"no answer from BB1 for {STALL_S:.0f} s ({e})")
            return None
        self.last_answer = time.time()
        return out

    def check(self, force=False):
        """Read /state (at most twice a second). Raises Stop if driving isn't safe."""
        if not force and time.time() - self.last_check < 0.5:
            return
        self.last_check = time.time()
        text = self.ask(get, f"{self.base}/state", timeout=1)
        if text is None:
            return
        s = json.loads(text)
        gains_versions(text, self.versions)          # remember gains changed mid-run (POST /gains)
        self.tick = s["tick"]
        if self.next_tick is None:
            self.next_tick = s["tick"] + 1            # record from now on
        if not s["armed"]:
            raise Stop(f"not balancing (reason: {s['reason'] or 'none'})")
        pos = (s["enc_l"] + s["enc_r"]) / 2 / COUNTS_PER_REV
        if self.home is None:
            self.home = pos
        self.travel = pos - self.home
        if abs(self.travel) > MAX_TRAVEL:
            raise Stop(f"{self.travel:+.1f} wheel turns from the start (limit {MAX_TRAVEL:g}): re-centre BB1")
        if s["enc_fault"]:
            raise Stop(f"encoder fault {s['enc_fault']} ({'left' if s['enc_fault'] == 1 else 'right'} encoder): check its wires")

    def record(self):
        """Pull new log records, about 4 times a second (like logger.py)."""
        if self.next_tick is None or time.time() - self.last_fetch < 0.25:
            return                     # (None: /state hasn't answered yet, so no start tick)
        self.last_fetch = time.time()
        while True:
            got = self.ask(fetch_log, self.base, self.next_tick)
            if got is None:
                return
            info, self.header, new = got
            self.rows += new
            self.lost += info["lost"]
            self.next_tick = info["to"]
            if info["to"] - info["from"] < MAX_PER_REQUEST:
                return

    def drive(self, speed, turn):
        ms = min(CMD_MS, MAX_CMD_MS)
        try:
            self.ask(post, f"{self.base}/drive", timeout=0.5, speed=speed, turn=turn, ms=ms)
        except urllib.error.HTTPError as e:     # 409: not balancing, or encoder fault
            raise Stop(f"/drive refused: {e}")

    def stop(self):
        """Ask BB1 to stop driving. Tries 3 times; the CMD_MS failsafe covers the rest."""
        for _ in range(3):
            try:
                post(f"{self.base}/drive/stop", timeout=1)
                return
            except OSError:
                time.sleep(0.1)

    def step(self, name, speed, turn, secs):
        moving = speed or turn
        print(f"  {name:20s} {secs:4.0f} s" + (f"  speed {speed:+.2f}  turn {turn:+.2f}" if moving else ""))
        end = time.time() + secs
        while time.time() < end:
            if moving:
                self.drive(speed, turn)
            self.check()
            self.record()
            time.sleep(RESEND_S)
        if moving:
            self.stop()                # stop between steps, don't wait for the failsafe


def run_test(base, test, note, surface, out):
    started = datetime.now()
    path = log_path(note or f"drive {test}", started, out)
    run = Run(base)
    meta = get(f"{base}/log/meta")
    try:
        run.check(force=True)          # refuse to start unless armed and no encoder fault
    except Stop as e:
        sys.exit(f"Not starting: {e}")
    steps, why = [], "done"
    print(f"Test '{test}', recording into {path.name}. Ctrl-C stops driving.")
    current = None                     # the step running now, kept even if it's cut short (a fall)
    try:
        for name, speed, turn, secs in TESTS[test]:
            run.check(force=True)
            current = (name, speed, turn, run.tick)
            run.step(name, speed, turn, secs)
            run.check(force=True)
            steps.append(current + (run.tick,))
            current = None
    except Stop as e:
        why = f"stopped: {e}"
    except KeyboardInterrupt:
        why = "stopped: Ctrl-C"
    except Exception as e:             # anything else: still stop driving and keep the log
        why = f"stopped: error {e!r}"
    finally:
        run.stop()
    print(f"  {why}. Ended {run.travel:+.1f} wheel turns from the start (forward +).")
    # Keep recording 2 s more so a fall or the stop is in the file. Falls are kept: they're data too.
    t_end = time.time() + 2
    while time.time() < t_end:
        try:
            run.last_fetch = 0
            run.record()
        except OSError:
            pass
        time.sleep(0.25)
    if current and run.rows:           # the cut-short step ends at the last record we got
        steps.append(current + (int(run.rows[-1].split(",", 1)[0]),))
    meta += "".join(f"# step,{n},{s:.2f},{t:.2f},{a},{b}\n" for n, s, t, a, b in steps)
    meta += f"# drive_test,{test},{why.replace(',', ';')}\n"
    save_log(path, meta, note or f"drive {test}", started, run.header, run.rows, run.lost, run.versions, surface)
    return path


def report(path):
    """One line per step: what was asked, what the wheels did, how it stopped."""
    df, _ = load_dump(path)
    steps = [l.split(",") for l in path.read_text().splitlines() if l.startswith("# step,")]
    if not steps:
        print("No '# step' lines: not a drive_test.py log. Use pid_report.py.")
        return
    pos = (df.enc_l + df.enc_r) / 2 / COUNTS_PER_REV        # wheel turns, forward +
    head = (df.enc_l - df.enc_r) / COUNTS_PER_REV           # left-minus-right turns (the heading)
    print(f"{'step':20s} {'asked':>11s} {'got spd':>7s} {'got trn':>7s} {'stop':>5s} {'head':>5s} "
          f"{'lean+':>5s} {'lean-':>5s} {'pwm':>4s} {'sat%':>4s} {'drop':>4s} flags")
    for i, (_, name, speed, turn, a, b) in enumerate(steps):
        speed, turn, a, b = float(speed), float(turn), int(a), int(b)
        g = df[(df.tick >= a) & (df.tick <= b)]
        if g.empty:
            continue
        lean = g.pitch - g.setpoint
        # Real speed over the last half of the step (after the ramp), turns/s, from the
        # encoder counts. Not from speed_l/speed_r: those read 3.3x low (CHANGELOG 2026-10-26)
        late = g.iloc[len(g) // 2:]
        secs = (late.t_us.iloc[-1] - late.t_us.iloc[0]) / 1e6 or 1
        got_s = (pos[late.index[-1]] - pos[late.index[0]]) / secs
        got_t = (head[late.index[-1]] - head[late.index[0]]) / secs
        # Stop: the furthest it rolled on (or kept turning) past where the command ended,
        # during the next pause, in turns. Driving: wheel turns; turning on the spot: heading.
        stop = turn_drift = np.nan
        if (speed or turn) and i + 1 < len(steps):
            nxt = df[(df.tick >= b) & (df.tick <= int(steps[i + 1][5]))]
            if not nxt.empty:
                track = pos if speed else head
                stop = ((track[nxt.index] - track[nxt.index[0]]) * np.sign(speed or turn)).max()
                turn_drift = head[nxt.index[-1]] - head[nxt.index[0]]
        # Heading: during a straight or still step, how much it turned anyway
        if not turn:
            turn_drift = head[g.index[-1]] - head[g.index[0]] if np.isnan(turn_drift) else turn_drift
        flags = int(np.bitwise_or.reduce(g.flag_bits.astype(int).values))
        # Seconds of a drive step with no command in force (Wi-Fi stall: the command ran out).
        # The first 0.25 s is skipped: the first command takes a moment to arrive.
        drop = ((g.iloc[50:].drive_speed == 0) & (g.iloc[50:].drive_turn == 0)).sum() / 200 if (speed or turn) else 0
        print(f"{name:20s} {speed:+5.2f} {turn:+5.2f} {got_s:+7.2f} {got_t:+7.2f} {stop:+5.2f} {turn_drift:+5.2f} "
              f"{lean.max():5.1f} {lean.min():5.1f} {g[['pwm_l', 'pwm_r']].abs().max().max():4.0f} "
              f"{100 * ((g.flag_bits & FLAG_SAT) > 0).mean():4.0f} {drop:4.1f} {flags}"
              + ("  ENCODER FAULT" if flags & FLAG_ENC_FAULT else ""))
    print("\nasked: speed, turn (turns/s). got: wheel average over the step's last half. "
          "stop: furthest it rolled (or turned) on after the command ended, turns (goal under 0.5). head: turns of "
          "left-minus-right drift after stopping, or during a still step (goal near 0). "
          "lean: max forward/back of pitch minus setpoint, deg. sat%: ticks at the PWM cap (flag 8). "
          "drop: seconds the command ran out mid-step (Wi-Fi stall): don't compare gains on a step with drops.")
    for line in path.read_text().splitlines():
        if line.startswith(("# drive_test,", "# gains_v")):
            print(line[2:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("test", nargs="?", choices=list(TESTS), help="which test to run")
    ap.add_argument("--host", default="bb1.local", help="robot address, optionally with :port")
    ap.add_argument("-n", "--note", default="", help="note saved in the file and its name")
    ap.add_argument("--surface", default="wood", help="what BB1 stands on (default wood)")
    ap.add_argument("-o", "--out", help="output file (default logs/bb1_<date>_<time>_<note>.csv)")
    ap.add_argument("--list", action="store_true", help="print the steps of every test")
    ap.add_argument("--report", metavar="LOG", help="report a saved drive_test.py log instead of running")
    args = ap.parse_args()
    if args.list:
        for k, steps in TESTS.items():
            print(f"{k}: " + ", ".join(f"{n} {s:g}s" for n, _, _, s in steps))
        return
    if args.report:
        report(find_file(args.report))
        return
    if not args.test:
        ap.error("name a test (or --list)")
    name, _, port = args.host.partition(":")
    try:
        ip = socket.gethostbyname(name)       # look up bb1.local once: .local lookups can be slow
    except OSError:
        sys.exit(f"Can't find {name}. Try --host <ip> (type 'wifi' on serial).")
    path = run_test(f"http://{ip}{':' + port if port else ''}", args.test, args.note, args.surface, args.out)
    print()
    report(path)


if __name__ == "__main__":
    main()
