"""drive_explore.py - drive BB1 around a box for a long time, to collect training logs.

Usage (BB1 balancing in the middle of a clear box on the floor):
    tools/venv/bin/python tools/drive_explore.py --box 1.5 --minutes 60 --surface wood
    tools/venv/bin/python tools/drive_explore.py --box 1.5 --minutes 1 --host localhost:8080   # mock

Moves BB1. It picks random drive commands (straight, reverse, spin, arc,
stop) for 1-4 s each, at most --max-speed, and keeps BB1 inside a square box
of --box metres around where it started: it tracks x, y and heading from the
encoders (odometry) and, near the edge, turns toward the middle and drives
back. Every command is a "# step" line in the log, like drive_test.py.

Safety (same commands as drive_test.py: 1 s each, re-sent every 100 ms):
  - stops for good on a fall, an encoder fault, battery under --min-cell volts
    per cell, or BB1 past the box edge (odometry drifts: the box is shrunk to
    80% to leave room)
  - a Wi-Fi stall only pauses driving (BB1 stops by itself within 1 s); after
    30 s with no answer it gives up
  - Ctrl-C stops driving and saves

Saves a complete log file every --chunk minutes (logs/bb1_<time>_explore-pN.csv),
so a crash or a fall loses at most one chunk's tail.
"""

import argparse
import json
import math
import random
import socket
import sys
import time
from datetime import datetime

import drive_test
from drive_test import COUNTS_PER_REV, Run, Stop
from logger import get, log_path, save_log

WHEEL_TURN_M = 0.178   # metres per wheel turn: rolled one turn on the wood floor, 177.8 mm (Hector, 2026-10-26; = 56.6 mm wheel)
TRACK_M = 0.17         # metres between the wheels (assumed, not measured)
SAFE_BOX = 0.8         # use 80% of the box: odometry drifts


class Odometry:
    """x, y (metres, x = forward at the start) and heading (rad, + = left) from the encoders."""

    def __init__(self, enc_l, enc_r):
        self.x = self.y = self.th = 0.0
        self.l, self.r = enc_l, enc_r

    def update(self, enc_l, enc_r):
        dl = (enc_l - self.l) / COUNTS_PER_REV * WHEEL_TURN_M
        dr = (enc_r - self.r) / COUNTS_PER_REV * WHEEL_TURN_M
        self.l, self.r = enc_l, enc_r
        d = (dl + dr) / 2
        self.th += (dr - dl) / TRACK_M          # right wheel further = turned left
        self.x += d * math.cos(self.th)
        self.y += d * math.sin(self.th)

    def home_angle(self):
        """Angle to turn (rad, + = left) to face the middle of the box."""
        a = math.atan2(-self.y, -self.x) - self.th
        return math.atan2(math.sin(a), math.cos(a))


def pick_move(max_speed):
    """A random (name, speed, turn, seconds). turn + = right, like /drive."""
    s = round(random.uniform(0.15, max_speed), 2)
    t = round(random.uniform(0.2, 0.6), 2)
    kind = random.choices(["fwd", "back", "spin", "arc", "stop"], weights=[3, 2, 2, 3, 2])[0]
    secs = round(random.uniform(1.0, 4.0), 1)
    if kind == "fwd":
        return f"fwd {s}", s, 0.0, secs
    if kind == "back":
        return f"back {s}", -s, 0.0, secs
    if kind == "spin":
        t *= random.choice([-1, 1])
        return f"spin {t:+}", 0.0, t, min(secs, 2.5)
    if kind == "arc":
        t *= random.choice([-1, 1])
        sign = random.choice([1, 1, -1])
        return f"arc {sign * s:+} {t:+}", sign * s, t, secs
    return "stop", 0.0, 0.0, secs


class Explorer:
    def __init__(self, base, args):
        self.base, self.args = base, args
        self.half = args.box / 2 * SAFE_BOX
        self.run = Run(base)
        self.steps = []                 # (name, speed, turn, from_tick, to_tick) for this chunk
        self.chunk = 1
        self.chunk_start = datetime.now()
        self.meta = get(f"{base}/log/meta")

    def state(self):
        s = json.loads(get(f"{self.base}/state", timeout=1))
        self.odo.update(s["enc_l"], s["enc_r"])
        return s

    def save_chunk(self, why=""):
        r = self.run
        if not r.rows:
            return
        meta = self.meta + "".join(f"# step,{n},{s:.2f},{t:.2f},{a},{b}\n" for n, s, t, a, b in self.steps)
        meta += f"# drive_explore,box {self.args.box} m,max_speed {self.args.max_speed},chunk {self.chunk}{(',' + why) if why else ''}\n"
        note = f"{self.args.note} explore p{self.chunk}".strip()
        out = None
        if self.args.out_dir:            # e.g. a scratch folder for mock runs: mock data never goes in logs/
            out = f"{self.args.out_dir}/{log_path(note, self.chunk_start).name}"
        save_log(log_path(note, self.chunk_start, out), meta, note, self.chunk_start, r.header, r.rows, r.lost, r.versions, self.args.surface)
        r.rows, r.lost, self.steps = [], 0, []
        self.chunk += 1
        self.chunk_start = datetime.now()
        self.meta = get(f"{self.base}/log/meta")      # fresh gains line for the next chunk

    def drive_for(self, name, speed, turn, secs):
        """Run one move. Returns when it's done or the edge is near (driving away from the middle)."""
        r = self.run
        start = r.tick
        end = time.time() + secs
        moving = speed or turn
        while time.time() < end:
            if moving:
                r.drive(speed, turn)
            r.check()
            r.record()
            if time.time() - getattr(self, "last_state", 0) > 0.25:   # 4 a second: keep the Pico's load like the page's
                self.last_state = time.time()
                self.watch(self.state())
            # Heading out of the box: cut the move short so the next one brings it back
            if moving and self.outside(0.9) and speed and math.cos(self.odo.home_angle()) * speed < 0:
                break
            time.sleep(drive_test.RESEND_S)
        if moving:
            r.stop()
        self.steps.append((name, speed, turn, start, r.tick))

    def outside(self, frac):
        return max(abs(self.odo.x), abs(self.odo.y)) > self.half * frac

    def watch(self, s):
        """Stop for good on anything that needs a person."""
        cell = s["batt_v"] / 2
        if s["batt_ok"] and cell < self.args.min_cell:
            raise Stop(f"battery {cell:.2f} V/cell, under {self.args.min_cell}")
        if max(abs(self.odo.x), abs(self.odo.y)) > self.args.box / 2:
            raise Stop(f"past the box edge (x {self.odo.x:+.2f} m, y {self.odo.y:+.2f} m)")

    def go_home(self):
        """Near the edge: turn to face the middle, then drive toward it."""
        a = self.odo.home_angle()
        if abs(a) > math.radians(25):
            # turn + = right = heading goes down; at about 0.4 turns/s difference that's
            # roughly 0.4 * 0.178 / 0.17 = 0.42 rad/s, so spin about |a| / 0.5 seconds
            self.drive_for("home turn", 0.0, -0.4 if a > 0 else 0.4, min(3.0, abs(a) / 0.5))
        dist = math.hypot(self.odo.x, self.odo.y)
        self.drive_for("home fwd", 0.25, 0.0, min(4.0, dist / (0.25 * WHEEL_TURN_M)))

    def main(self):
        r = self.run
        r.check(force=True)             # refuses unless armed and no encoder fault
        s = json.loads(get(f"{self.base}/state"))
        self.odo = Odometry(s["enc_l"], s["enc_r"])
        t_end = time.time() + self.args.minutes * 60
        why = "done"
        print(f"Exploring a {self.args.box} m box for {self.args.minutes:g} min. Ctrl-C stops.")
        try:
            while time.time() < t_end:
                try:
                    if self.outside(0.7):
                        self.go_home()
                    else:
                        self.drive_for(*pick_move(self.args.max_speed))
                except Stop as e:
                    if "no answer" not in str(e):
                        raise
                    # Wi-Fi stall: BB1 has already stopped by itself. Wait and try again.
                    print(f"  {datetime.now():%H:%M:%S} Wi-Fi: {e}; waiting")
                    for _ in range(10):
                        time.sleep(3)
                        try:
                            r.check(force=True)
                            break
                        except Stop as e2:
                            if "no answer" not in str(e2):
                                raise
                    else:
                        raise Stop("no answer from BB1 for 30 s")
                if (datetime.now() - self.chunk_start).total_seconds() > self.args.chunk * 60:
                    self.save_chunk()
                if time.time() - getattr(self, "last_print", 0) > 30:
                    self.last_print = time.time()
                    print(f"  {datetime.now():%H:%M:%S} x {self.odo.x:+.2f} y {self.odo.y:+.2f} m, "
                          f"heading {math.degrees(self.odo.th) % 360:3.0f} deg, batt {s['batt_v'] / 2:.2f} V/cell")
                    s = json.loads(get(f"{self.base}/state"))
        except Stop as e:
            why = f"stopped: {e}"
        except KeyboardInterrupt:
            why = "stopped: Ctrl-C"
        except Exception as e:
            why = f"stopped: error {e!r}"
        finally:
            r.stop()
        print(f"  {why}")
        t = time.time() + 2               # 2 s more so a fall is in the file
        while time.time() < t:
            try:
                r.last_fetch = 0
                r.record()
            except Exception:
                pass
            time.sleep(0.25)
        self.save_chunk(why.replace(",", ";"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--box", type=float, required=True, help="side of the clear square, metres")
    ap.add_argument("--minutes", type=float, default=60)
    ap.add_argument("--max-speed", type=float, default=0.4, help="turns/s (default 0.4)")
    ap.add_argument("--min-cell", type=float, default=3.5, help="stop under this many volts per cell")
    ap.add_argument("--chunk", type=float, default=5, help="save a log file every this many minutes")
    ap.add_argument("--host", default="bb1.local")
    ap.add_argument("--surface", default="wood")
    ap.add_argument("-n", "--note", default="", help="note in every chunk's file and name")
    ap.add_argument("--seed", type=int, help="random seed (to repeat a run)")
    ap.add_argument("--out-dir", help="save the chunks here instead of logs/ (use it for the mock)")
    args = ap.parse_args()
    random.seed(args.seed)
    drive_test.MAX_TRAVEL = 1e9          # the box check replaces drive_test's 3-turn limit
    name, _, port = args.host.partition(":")
    try:
        ip = socket.gethostbyname(name)
    except OSError:
        sys.exit(f"Can't find {name}. Try --host <ip>.")
    try:
        Explorer(f"http://{ip}{':' + port if port else ''}", args).main()
    except Stop as e:
        sys.exit(f"Not starting: {e}")


if __name__ == "__main__":
    main()
