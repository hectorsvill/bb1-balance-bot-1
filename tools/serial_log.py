"""serial_log.py - read the Pico's USB serial for a while and save what it prints.

Usage:
    tools/venv/bin/python tools/serial_log.py                 # 15 s, first Pico found
    tools/venv/bin/python tools/serial_log.py -t 30 -o run.txt
    tools/venv/bin/python tools/serial_log.py -s "cal"        # send a command first

Unlike `arduino-cli monitor`, this stops by itself, so it's handy for
capturing a test run to paste into a log note.
"""

import argparse
import glob
import sys
import time
from pathlib import Path

import serial  # pyserial
from serial.tools import list_ports

REPO = Path(__file__).resolve().parent.parent   # the repo folder (tools/ is one level down)


PICO_USB_VENDOR = 0x2E8A   # Raspberry Pi's USB vendor ID: every Pico reports it


def find_port():
    """The Pico's serial port. Other USB gadgets (a hub, a dock) can also show
    up as /dev/cu.usbmodemXXXX, so pick the one whose USB vendor is Raspberry Pi
    instead of just the first in the list."""
    picos = [p.device for p in list_ports.comports() if p.vid == PICO_USB_VENDOR]
    if picos:
        return sorted(picos)[0]
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No Pico found (ls /dev/cu.usbmodem* is empty). Is USB plugged in?")
    print(f"No port says Raspberry Pi; trying {ports[0]} (use -p to pick another)")
    return ports[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (default: first /dev/cu.usbmodem*)")
    ap.add_argument("-t", "--seconds", type=float, default=15, help="how long to read (default 15)")
    ap.add_argument("-o", "--out", help="also save to this file (a relative path is inside the repo folder, e.g. logs/run.txt)")
    ap.add_argument("-s", "--send", help="send this line (plus newline) after opening")
    args = ap.parse_args()

    port = args.port or find_port()
    out = None
    if args.out:
        # Relative paths are taken from the repo folder, not from wherever the
        # terminal happens to be, so logs/... always lands in the repo's logs/.
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = REPO / out_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out = open(out_path, "w")

    # timeout=0.2: readline() gives up after 0.2 s with no data, so the
    # time limit below is checked often even when the Pico is quiet.
    with serial.Serial(port, 115200, timeout=0.2) as ser:
        if args.send:
            ser.write((args.send + "\n").encode())
        end = time.time() + args.seconds
        while time.time() < end:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").rstrip()
            print(line)
            if out:
                out.write(line + "\n")
    if out:
        out.close()
        print(f"saved to {out_path}")


if __name__ == "__main__":
    main()
