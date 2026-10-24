"""handheld.py - run an arm test from one window: keys to arm and stop, log saved by itself.

How it works inside: docs/HANDHELD_TOOL.md.

Usage (from the repo folder, USB plugged in, BB1 on the home network):
    tools/venv/bin/python tools/handheld.py                    # arm 5 s per press of "a"
    tools/venv/bin/python tools/handheld.py --wifi -s 10       # no USB cable: everything over Wi-Fi
    tools/venv/bin/python tools/handheld.py -s 3 -n "tilt"     # 3 s per arm, note "tilt"
    tools/venv/bin/python tools/handheld.py --host 192.168.1.240
    tools/venv/bin/python tools/handheld.py --cap 150 --kp 18  # set these first (not saved in flash)

Keys (no Enter needed):
    a        arm for a few seconds (default 5), it disarms by itself after that
    Enter    disarm now (so does space)
    g        print the balance settings (gains)
    :        type any serial command, then Enter (e.g. "kp 20", "kd 0.6", "trim here", "save")
    q        disarm, save the log and quit (Ctrl-C does the same)

What it does: two helpers run side by side.
  - Serial (USB): sends your commands and prints BB1's answers (ARMED,
    DISARMED: why). The 10 Hz readout lines are not printed one per line:
    they only update one status line (pitch, how far from the trim), so the
    screen stays readable.
  - Wi-Fi: records every 5 ms log record, exactly like logger.py, and saves
    the same file layout when you quit (logs/bb1_<date>_<time>_<note>.csv).

Arming from serial means the web page heartbeat doesn't apply (keep the page
closed). The firmware's other cut-outs still work: the arm timer, 45 deg
(fell over), IMU errors, late loops.

--wifi: no USB cable (it tugs on the robot). Commands go over HTTP instead
(POST /arm, /disarm, /gains, the same endpoints as the web page), and the
status line comes from GET /state ten times a second. Those /state requests
are also the heartbeat: armed over Wi-Fi, BB1 disarms by itself if it hears
nothing for 2 s ("lost the web page", HEARTBEAT_MS in config.h). That's the safety net when there's no
cable, so a Wi-Fi hiccup can cut a try short.
"""

import argparse
import json
import select
import shutil
import socket
import sys
import termios
import threading
import time
import tty
from datetime import datetime

import serial  # pyserial

from logger import MAX_PER_REQUEST, fetch_log, get, log_path, post, save_log
from serial_log import find_port


class WifiRecorder(threading.Thread):
    """Keeps asking BB1 for new log records (/log?since=N), like logger.py."""

    def __init__(self, base):
        super().__init__(daemon=True)
        self.base = base
        self.meta = get(f"{base}/log/meta")                       # the "# setting" lines
        self.next_tick = json.loads(get(f"{base}/state"))["tick"] + 1   # start from "now"
        self.rows, self.lost, self.header = [], 0, ""
        self.problem = ""                                          # shown in the status line
        self.running = True

    def run(self):
        while self.running:
            try:
                info, self.header, new = fetch_log(self.base, self.next_tick)
                self.problem = ""
            except OSError:
                self.problem = "Wi-Fi: no answer, retrying"
                time.sleep(0.5)
                continue
            self.rows += new
            self.lost += info["lost"]
            self.next_tick = info["to"]
            if info["to"] - info["from"] < MAX_PER_REQUEST:
                time.sleep(0.25)          # caught up: wait a little before asking again


class SerialLink(threading.Thread):
    """Reads BB1's serial output. Readout lines update self.readout; anything
    else (answers to commands, ARMED, DISARMED) is printed in full."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self.readout = ""
        self.trim = None                          # from the "gains" answer, to show the lean from the trim
        self.gains = ""                           # "kp 15 kd 0.5 cap 80", shown in the status line
        self.versions = {}                        # every gains version seen: {3: "kp 15 ki 0 kd 0.5 ..."}, saved in the log
        self.running = True

    def send(self, cmd):
        self.ser.write((cmd + "\n").encode())

    def run(self):
        while self.running:
            line = self.ser.readline().decode(errors="replace").rstrip()
            if not line:
                continue
            if line.startswith("pitch "):            # the 10 Hz readout (see printStatus in bb1.ino)
                pitch = float(line.split("|")[0].split()[1])    # "pitch  -3.9 | ..." -> -3.9
                self.readout = f"pitch {pitch:+.1f}"
                if self.trim is not None:            # how far from the balance point: arming needs within 10
                    self.readout += f" (trim {pitch - self.trim:+.1f})"
                if "| batt " in line:                # "... | batt 7.92 V" (firmware from 2026-10-24)
                    self.readout += " | batt " + line.split("| batt ")[1].strip()
            else:
                if line.startswith("gains") and " trim " in line:   # "gains v2: kp 15.00 ... trim -3.98 deg ..."
                    self.trim = float(line.split(" trim ")[1].split()[0])
                    w = line.split()
                    pick = lambda key: f"{float(w[w.index(key) + 1]):g}"    # the number after a word
                    self.gains = f"kp {pick('kp')} kd {pick('kd')} cap {pick('cap')}"
                    # The CSV only has gains_ver per row, so keep what each version
                    # meant (changes from the /tune page arrive here too).
                    ver = int(w[1].lstrip("v").rstrip(":"))
                    self.versions[ver] = " ".join(f"{k} {pick(k)}" for k in ("kp", "ki", "kd", "trim", "cap", "deadband"))
                say(f"BB1: {line}")


class WifiLink(threading.Thread):
    """Same job as SerialLink, but over Wi-Fi (handheld.py --wifi).

    Polls GET /state ten times a second: that updates the status line and
    keeps BB1's heartbeat alive while armed. send() turns the serial-style
    commands into the web page's POST requests."""

    POLL_S = 0.1                 # /state every 100 ms; BB1 disarms after 2 s without one

    def __init__(self, base):
        super().__init__(daemon=True)
        self.base = base
        self.readout = ""
        self.trim = None
        self.gains = ""
        self.versions = {}       # same as SerialLink: saved in the log
        self.running = True
        self.armed = None        # last armed state seen, to print ARMED / DISARMED once

    def post(self, path, **fields):
        try:
            return post(f"{self.base}{path}", timeout=2, **fields)
        except OSError as e:
            return f"no answer ({e})"

    def send(self, cmd):
        """The same commands as serial, sent as web requests."""
        words = cmd.split()
        if not words:
            return
        names = {"p": "kp", "i": "ki", "d": "kd"}                 # "p 10" = "kp 10", like serial
        key = names.get(words[0], words[0])
        if key == "arm":
            say("BB1: " + self.post("/arm", seconds=words[1] if len(words) > 1 else 0))
        elif key == "disarm":
            self.post("/disarm")
        elif key == "gains":
            say(f"BB1: gains: {self.versions.get(max(self.versions), '...') if self.versions else '...'}")
        elif key in ("kp", "ki", "kd", "trim", "cap", "deadband") and len(words) == 2:
            say("BB1: " + self.post("/gains", **{key: words[1]}))      # "trim here" works too
        elif key == "save":
            say("BB1: " + self.post("/gains/save"))
        elif key == "defaults":
            say("BB1: " + self.post("/gains/defaults"))
        else:
            say(f"'{cmd}' only works over USB (try: arm, disarm, gains, kp/ki/kd/trim/cap N, save, defaults)")

    def run(self):
        while self.running:
            try:
                s = json.loads(get(f"{self.base}/state", timeout=0.8))
            except (OSError, ValueError):
                self.readout = "Wi-Fi: no answer"
                time.sleep(self.POLL_S)
                continue
            g = s["gains"]
            self.trim = g["trim"]
            self.readout = f"pitch {s['pitch']:+.1f} (trim {s['pitch'] - g['trim']:+.1f})"
            if s.get("batt_ok"):
                self.readout += f" | batt {s['batt_v'] / s.get('batt_cells', 2):.2f} V/cell"
            self.gains = f"kp {g['kp']:g} kd {g['kd']:g} cap {g['cap']:g}"
            self.versions[g["ver"]] = " ".join(f"{k} {g[k]:g}" for k in ("kp", "ki", "kd", "trim", "cap", "deadband"))
            if s["armed"] != self.armed:                           # print each change once
                if self.armed is not None:
                    say("BB1: ARMED" if s["armed"] else f"BB1: DISARMED: {s['reason']}")
                self.armed = s["armed"]
            time.sleep(self.POLL_S)


def say(text):
    """Print a full line above the status line. \\r and the clear code
    (ESC[K) wipe the status line first; in cbreak mode \\n alone doesn't
    go back to the left edge, so we add \\r."""
    sys.stdout.write("\r\x1b[K" + text + "\r\n")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="bb1.local", help="robot address (default bb1.local; or the IP from 'wifi' on serial)")
    ap.add_argument("-p", "--port", help="serial port (default: the Pico's /dev/cu.usbmodem*)")
    ap.add_argument("--wifi", action="store_true", help="no USB cable: arm, disarm and gains over Wi-Fi")
    ap.add_argument("--surface", default="unknown", help="what BB1 stands on: wood, carpet, air... (saved in the file)")
    ap.add_argument("-s", "--seconds", type=float, default=5, help="arm time per press of 'a' (default 5, max 10 here)")
    ap.add_argument("-n", "--note", default="handheld", help="a note saved in the file and its name (default handheld)")
    # Settings sent at the start, so the file name and the run match what you asked for.
    ap.add_argument("--kp", type=float, help="set kp before the test")
    ap.add_argument("--kd", type=float, help="set kd before the test")
    ap.add_argument("--cap", type=float, help="set the PWM cap before the test (of 255)")
    ap.add_argument("--trim", help="set the trim before the test (a number, or here)")
    args = ap.parse_args()
    arm_s = min(max(args.seconds, 1), 10)            # a short timer: every motor run stops by itself

    try:
        ip = socket.gethostbyname(args.host)           # look up bb1.local once; the IP is instant after that
    except OSError:
        sys.exit(f"Can't find {args.host}. Is BB1 on and on this network? Try --host <ip> (type 'wifi' on serial).")
    started = datetime.now()
    out = log_path(args.note, started)

    link = WifiLink(f"http://{ip}") if args.wifi else SerialLink(args.port or find_port())
    rec = WifiRecorder(f"http://{ip}")
    link.start()
    rec.start()

    print(f"Recording from {args.host} ({ip}). Log will be saved to {out.name}")
    print(f"Keys: a = arm {arm_s:g} s   Enter/space = disarm   g = gains   : = type a command   q = save and quit")
    if args.wifi:
        print("Wi-Fi mode: no USB needed. If Wi-Fi drops for 2 s while armed, BB1 disarms by itself.")
    print("Hold BB1 in the air, wheels free, fingers clear.")
    for name in ("kp", "kd", "cap", "trim"):         # only the ones you gave
        value = getattr(args, name)
        if value is not None:
            link.send(f"{name} {value:g}" if isinstance(value, float) else f"{name} {value}")
            time.sleep(0.1)                            # one command at a time
    link.send("gains")

    # cbreak: each key arrives at once, without waiting for Enter, and isn't echoed.
    old = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin)
    shown = ""                                         # the status line now on screen
    try:
        while True:
            # Wait up to 0.2 s for a key, then refresh the status line either way.
            if select.select([sys.stdin], [], [], 0.2)[0]:
                key = sys.stdin.read(1)
                if key == "a":
                    link.send(f"arm {arm_s:g}")
                elif key in ("\n", " "):
                    link.send("disarm")
                elif key == "g":
                    link.send("gains")
                elif key == ":":
                    # Back to normal typing for one line, so you can send any
                    # serial command (gains, trim, save...). BB1's answer prints as usual.
                    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
                    sys.stdout.write("\r\x1b[Kcommand: ")
                    sys.stdout.flush()
                    cmd = sys.stdin.readline().strip()
                    tty.setcbreak(sys.stdin)
                    if cmd:
                        link.send(cmd)
                    shown = ""                         # the prompt replaced the status line: draw it again
                elif key == "q":
                    break
            status = f"{link.readout or 'waiting for BB1'} | {link.gains} | log {len(rec.rows) / 200:.0f} s"
            if rec.lost:
                status += f", {rec.lost} lost"
            if rec.problem:
                status += f"  | {rec.problem}"
            # Cut it to the window width: a line that wraps can't be redrawn in
            # place (\r only goes back to the start of the last row), and every
            # refresh would leave a new line behind, which looks like spam.
            width = shutil.get_terminal_size().columns - 1
            if status != shown:                        # redraw only when something changed
                sys.stdout.write("\r\x1b[K" + status[:width])
                sys.stdout.flush()
                shown = status
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)   # give the terminal back as it was
        link.send("disarm")                                     # never leave it armed
        time.sleep(0.5)                                         # let the last records arrive
        rec.running = False
        link.running = False
        print()
        save_log(out, rec.meta, args.note, started, rec.header, rec.rows, rec.lost, link.versions, args.surface)


if __name__ == "__main__":
    main()
