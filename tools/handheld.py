"""handheld.py - run an arm test from one window: keys to arm and stop, log saved by itself.

Usage (from the repo folder, USB plugged in, BB1 on the home network):
    tools/venv/bin/python tools/handheld.py                    # arm 5 s per press of "a"
    tools/venv/bin/python tools/handheld.py -s 3 -n "tilt"     # 3 s per arm, note "tilt"
    tools/venv/bin/python tools/handheld.py --host 192.168.1.240

Keys (no Enter needed):
    a        arm for a few seconds (default 5), it disarms by itself after that
    Enter    disarm now (so does space)
    g        print the balance settings (gains)
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
"""

import argparse
import json
import select
import socket
import sys
import termios
import threading
import time
import tty
from datetime import datetime

import serial  # pyserial

from logger import MAX_PER_REQUEST, fetch_log, get, log_path, save_log
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
                    self.readout += f" ({pitch - self.trim:+.1f} from trim)"
            else:
                if line.startswith("gains") and " trim " in line:   # "gains v2: kp 15.00 ... trim -3.98 deg ..."
                    self.trim = float(line.split(" trim ")[1].split()[0])
                say(f"BB1: {line}")


def say(text):
    """Print a full line above the status line. \\r and the clear code
    (ESC[K) wipe the status line first; in cbreak mode \\n alone doesn't
    go back to the left edge, so we add \\r."""
    sys.stdout.write("\r\x1b[K" + text + "\r\n")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="bb1.local", help="robot address (default bb1.local; or the IP from 'wifi' on serial)")
    ap.add_argument("-p", "--port", help="serial port (default: first /dev/cu.usbmodem*)")
    ap.add_argument("-s", "--seconds", type=float, default=5, help="arm time per press of 'a' (default 5, max 10 here)")
    ap.add_argument("-n", "--note", default="handheld", help="a note saved in the file and its name (default handheld)")
    args = ap.parse_args()
    arm_s = min(max(args.seconds, 1), 10)            # a short timer: every motor run stops by itself

    try:
        ip = socket.gethostbyname(args.host)           # look up bb1.local once; the IP is instant after that
    except OSError:
        sys.exit(f"Can't find {args.host}. Is BB1 on and on this network? Try --host <ip> (type 'wifi' on serial).")
    started = datetime.now()
    out = log_path(args.note, started)

    link = SerialLink(args.port or find_port())
    rec = WifiRecorder(f"http://{ip}")
    link.start()
    rec.start()

    print(f"Recording from {args.host} ({ip}). Log will be saved to {out.name}")
    print(f"Keys: a = arm {arm_s:g} s   Enter/space = disarm   g = gains   q = save and quit")
    print("Hold BB1 in the air, wheels free, fingers clear.")
    link.send("gains")

    # cbreak: each key arrives at once, without waiting for Enter, and isn't echoed.
    old = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin)
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
                elif key == "q":
                    break
            status = f"{link.readout or 'waiting for serial'}  | log {len(rec.rows) / 200:.1f} s"
            if rec.lost:
                status += f", {rec.lost} lost"
            if rec.problem:
                status += f"  | {rec.problem}"
            sys.stdout.write("\r\x1b[K" + status)
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)   # give the terminal back as it was
        link.send("disarm")                                     # never leave it armed
        time.sleep(0.5)                                         # let the last records arrive
        rec.running = False
        link.running = False
        print()
        save_log(out, rec.meta, args.note, started, rec.header, rec.rows, rec.lost)


if __name__ == "__main__":
    main()
