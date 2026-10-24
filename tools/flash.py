"""flash.py - compile the bb1 firmware and put it on BB1 over USB, safely.

    tools/venv/bin/python tools/flash.py

Steps, each printed as it goes:
  1. compile firmware/bb1 (arduino-cli), stop if it doesn't build
  2. find BB1's USB port (the Pico's Raspberry Pi USB ID)
  3. safety check: read the pitch over serial. With auto-arm, BB1 starts
     balancing by itself after the reboot if it's upright, so it must be lying
     down (more than 45 deg from upright). Stops if it isn't.
  4. upload, wait for BB1 to come back, and show what it says: the auto-arm
     state and the gains loaded from flash

Options:
  --skip-compile   upload the last build (build/bb1) without compiling
  --force          skip the lying-down check (wheels off the table!)
  -p PORT          use this port instead of finding it

Close the Arduino IDE Serial Monitor, handheld.py and anything else holding
the port first: the upload fails while another program has it open.
Details: docs/FLASHING.md, "Flashing with tools/flash.py".
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

import serial

from serial_log import find_port   # picks the Pico by its USB ID

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "firmware" / "bb1"
BUILD = REPO / "build" / "bb1"
FQBN = "rp2040:rp2040:rpipicow"
LYING_DOWN_DEG = 45        # same as FALL_ANGLE in config.h


def step(text):
    print(f"\n== {text}")


def compile_firmware():
    step("Compiling firmware/bb1")
    if not (SKETCH / "secrets.h").exists():
        sys.exit("firmware/bb1/secrets.h is missing: copy secrets.example.h to secrets.h first.")
    result = subprocess.run(["arduino-cli", "compile", "--fqbn", FQBN, "--output-dir", str(BUILD), str(SKETCH)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-3000:], result.stderr[-3000:])
        sys.exit("Compile failed: nothing was flashed.")
    for line in result.stdout.splitlines():
        if line.startswith(("Sketch uses", "Global variables")):
            print(line)


def read_lines(port, seconds, send=None):
    """Everything BB1 prints for a few seconds (optionally after sending a command)."""
    with serial.Serial(port, 115200, timeout=0.2) as s:
        if send:
            time.sleep(0.3)
            s.write(send.encode() + b"\n")
        out, end = b"", time.time() + seconds
        while time.time() < end:
            out += s.read(4096)
    return out.decode(errors="replace").splitlines()


def check_lying_down(port):
    step("Safety check: is BB1 lying down?")
    lines = read_lines(port, 2.5)
    pitches = [float(m.group(1)) for l in lines if (m := re.match(r"pitch\s+([+-]?[\d.]+)", l))]
    trims = [float(m.group(1)) for l in read_lines(port, 1.5, "gains") if (m := re.search(r"trim\s+([+-]?[\d.]+)", l))]
    if not pitches:
        sys.exit("Couldn't read the pitch from BB1 (is something else holding the port?). "
                 "Lay it down and use --force to flash anyway.")
    pitch, trim = pitches[-1], trims[-1] if trims else 0.0
    print(f"pitch {pitch:+.1f} deg, trim {trim:+.1f} deg")
    if abs(pitch - trim) <= LYING_DOWN_DEG:
        sys.exit("BB1 is upright: after the reboot it would start balancing on the spot. "
                 "Lay it down (on its front) and run this again.")
    print("Lying down: safe to flash.")


def upload(port):
    step(f"Uploading to {port}")
    result = subprocess.run(["arduino-cli", "upload", "-p", port, "--fqbn", FQBN, "--input-dir", str(BUILD), str(SKETCH)],
                            capture_output=True, text=True)
    print((result.stdout + result.stderr).strip()[-1500:])
    if result.returncode != 0:
        sys.exit("Upload failed. Close any serial monitor and try again, or use BOOTSEL (docs/FLASHING.md).")


def show_status():
    step("BB1 after the reboot")
    for _ in range(20):                      # the port disappears and comes back
        time.sleep(0.5)
        try:
            port = find_port()
            break
        except SystemExit:
            continue
    else:
        sys.exit("BB1 didn't come back on USB. Unplug and replug it, then check with a serial monitor.")
    time.sleep(1.5)
    lines = read_lines(port, 6, "gains")
    wanted = ("auto-arm", "gains v", "Balance settings", "Gyro", "! ")
    for line in lines:
        if line.startswith(wanted):
            print(line)
    if any("auto-arm: waiting" in l for l in lines):
        print("\nDone: flashed, lying down and waiting. Unplug USB to use it.")
    else:
        print("\nFlashed. I didn't see 'auto-arm: waiting' yet: check the lines above.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skip-compile", action="store_true", help="upload the last build without compiling")
    ap.add_argument("--force", action="store_true", help="skip the lying-down check")
    ap.add_argument("-p", "--port", help="serial port (default: the Pico's)")
    args = ap.parse_args()

    if not args.skip_compile:
        compile_firmware()
    elif not any(BUILD.glob("*.uf2")):
        sys.exit("No build in build/bb1 yet: run without --skip-compile.")
    port = args.port or find_port()
    if not args.force:
        check_lying_down(port)
    upload(port)
    show_status()


if __name__ == "__main__":
    main()
