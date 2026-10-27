"""flash.py - compile the bb1 firmware and put it on BB1, safely: over USB, or over Wi-Fi.

    tools/venv/bin/python tools/flash.py          # USB cable
    tools/venv/bin/python tools/flash.py --ota    # Wi-Fi (BB1_HOST or bb1.local; --host <ip>)

Steps, each printed as it goes:
  1. compile firmware/bb1 (arduino-cli), stop if it doesn't build
  2. find BB1's USB port (the Pico's Raspberry Pi USB ID)
  3. safety check: read the pitch over serial. With auto-arm, BB1 starts
     balancing by itself after the reboot if it's upright, so it must be lying
     down (more than 45 deg from upright). Stops if it isn't.
  4. upload, wait for BB1 to come back, and show what it says: the auto-arm
     state and the gains loaded from flash

With --ota, steps 2-4 go over Wi-Fi instead (firmware/bb1/ota.h):
  2. read /state: disarmed, no motor test, lying down (BB1 checks it too)
  3. send build/bb1/bb1.ino.bin, gzipped, to POST /update with the password
     from firmware/bb1/secrets.h (BB1_OTA_PASSWORD)
  4. wait for BB1 to restart into it (/sys shows the new build time), then
     show the auto-arm state and the gains
The first OTA-capable firmware goes on over USB once: it changes the flash
layout (Flash Size "Sketch: 1MB, FS: 1MB", see FQBN). After that, either way works.

Options:
  --skip-compile   upload the last build (build/bb1) without compiling
  --force          skip the lying-down check (wheels off the table!)
  -p PORT          use this port instead of finding it
  --ota            over Wi-Fi instead of USB
  --host HOST      BB1's address for --ota (default $BB1_HOST or bb1.local)

Close the Arduino IDE Serial Monitor, handheld.py and anything else holding
the port first: the upload fails while another program has it open.
Details: docs/FLASHING.md, "Flashing with tools/flash.py".
"""

import argparse
import base64
import gzip
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

import serial

from serial_log import find_port   # picks the Pico by its USB ID

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "firmware" / "bb1"
BUILD = REPO / "build" / "bb1"
# flash=...: 1 MB for the program, 1 MB for LittleFS, where an OTA update waits
# before the bootloader copies it in (ota.h). Same in the CI build.
FQBN = "rp2040:rp2040:rpipicow:flash=2097152_1048576"
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


# ---------- over Wi-Fi (--ota) ----------

def ota_password():
    m = re.search(r'#define\s+BB1_OTA_PASSWORD\s+"([^"]*)"', (SKETCH / "secrets.h").read_text())
    if not m:
        sys.exit("BB1_OTA_PASSWORD is missing from firmware/bb1/secrets.h (see secrets.example.h).")
    return m.group(1)


def http_json(url, timeout=5, data=None, headers=None):
    """GET (or POST data) and decode BB1's JSON answer, even from an error status."""
    req = urllib.request.Request(url, data=data, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as res:
            return json.loads(res.read().decode())
    except urllib.error.HTTPError as e:
        return json.loads(e.read().decode() or "{}") | {"http": e.code}


def ota_check(base):
    step("Safety check over Wi-Fi: is BB1 lying down and disarmed?")
    try:
        s = http_json(f"{base}/state")
        sys_info = http_json(f"{base}/sys")
    except OSError as e:
        sys.exit(f"BB1 didn't answer at {base}: {e}. Try --host <ip> ('wifi' on serial shows it), or flash over USB.")
    if "fs_total" not in sys_info:
        sys.exit("BB1's firmware has no OTA yet: flash this one over USB once (without --ota).")
    pitch, trim = s["pitch"], s["gains"]["trim"]
    print(f"pitch {pitch:+.1f} deg, trim {trim:+.1f} deg, armed {s['armed']}, build {sys_info['build']}")
    if s["armed"] or s["test_running"]:
        sys.exit("BB1 is balancing or running a motor test: stop it and lay it down first.")
    if abs(pitch - trim) <= LYING_DOWN_DEG:
        sys.exit("BB1 is upright: after the restart it would start balancing on the spot. "
                 "Lay it down (on its front) and run this again.")
    print("Lying down: safe to flash.")
    return sys_info["build"]


def ota_upload(base):
    image = gzip.compress((BUILD / "bb1.ino.bin").read_bytes())   # the bootloader unzips it while copying
    step(f"Sending the firmware over Wi-Fi ({len(image) // 1024} KB gzipped)")
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"bb1.ino.bin.gz\"\r\n"
            f"Content-Type: application/octet-stream\r\n\r\n").encode() + image + f"\r\n--{boundary}--\r\n".encode()
    auth = base64.b64encode(f"bb1:{ota_password()}".encode()).decode()
    t0 = time.time()
    try:
        answer = http_json(f"{base}/update", timeout=120, data=body, headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}", "Authorization": f"Basic {auth}"})
    except OSError as e:
        sys.exit(f"The upload broke off ({e}). BB1 keeps its old firmware: run this again.")
    print(answer, f"({time.time() - t0:.1f} s)")
    if not answer.get("ok"):
        sys.exit("BB1 refused or the check failed: it keeps its old firmware.")


def ota_show_status(base, old_build):
    step("BB1 after the restart")
    end = time.time() + 60                     # copying 0.5 MB and joining Wi-Fi again takes a while
    while time.time() < end:
        time.sleep(2)
        try:
            build = http_json(f"{base}/sys", timeout=2)["build"]
        except (OSError, ValueError, KeyError):
            continue                           # still restarting
        if build != old_build:
            break
    else:
        sys.exit("BB1 didn't come back with the new firmware within 60 s. Check it over USB (serial), "
                 "or flash over USB. The old firmware still runs if the copy never started.")
    s = http_json(f"{base}/state")
    g = s["gains"]
    print(f"build {build}, uptime {s['uptime_s']} s, auto-arm: {s['auto_state']}")
    print(f"gains v{g['ver']}: kp {g['kp']:g} ki {g['ki']:g} kd {g['kd']:g} trim {g['trim']:g}")
    print("\nDone: flashed over Wi-Fi.")


def resolve(host):
    """Look bb1.local up once (.local lookups can be slow, and every request would pay it)."""
    name, _, port = host.partition(":")
    try:
        return f"http://{socket.gethostbyname(name)}{':' + port if port else ''}"
    except OSError:
        sys.exit(f"Can't find {name}: use --host <ip> ('wifi' on serial shows it).")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skip-compile", action="store_true", help="upload the last build without compiling")
    ap.add_argument("--force", action="store_true", help="skip the lying-down check")
    ap.add_argument("-p", "--port", help="serial port (default: the Pico's)")
    ap.add_argument("--ota", action="store_true", help="over Wi-Fi instead of USB")
    ap.add_argument("--host", default=os.environ.get("BB1_HOST", "bb1.local"), help="BB1's address for --ota")
    args = ap.parse_args()

    if not args.skip_compile:
        compile_firmware()
    elif not any(BUILD.glob("*.uf2")):
        sys.exit("No build in build/bb1 yet: run without --skip-compile.")
    if args.ota:
        base = resolve(args.host)
        old_build = ota_check(base) if not args.force else http_json(f"{base}/sys").get("build")
        ota_upload(base)
        ota_show_status(base, old_build)
        return
    port = args.port or find_port()
    if not args.force:
        check_lying_down(port)
    upload(port)
    show_status()


if __name__ == "__main__":
    main()
