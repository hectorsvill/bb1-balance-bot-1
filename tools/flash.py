"""flash.py - compile the bb1 firmware and put it on BB1, safely: over USB, or over Wi-Fi.

    tools/venv/bin/python tools/flash.py                   # plan only: one JSON document, nothing is flashed
    tools/venv/bin/python tools/flash.py --execute         # USB cable
    tools/venv/bin/python tools/flash.py --ota --execute   # Wi-Fi (BB1_HOST or bb1.local; --host <ip>)

Without --execute (alias --yes) this only prints the plan and exits 0:
target, transport (usb or ota), host, firmware path, and whether the
lying-down safety check will run. It does not compile, upload, or open
the serial port.

With --execute it does the real compile and upload. Steps, each printed
as it goes:
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
  4. wait for BB1 to restart into it (/sys shows the new build id), then
     show the auto-arm state and the gains

--force skips that lying-down check (wheels off the table). It is refused
unless --execute is also passed: the plan then says so and exits 2, and
nothing is flashed. The plan's warnings say the check will be skipped.

Versions: before compiling it writes firmware/bb1/build_id.h (gitignored):
branch, git commit, "+" if there are uncommitted changes, and the time. BB1
reports it with FIRMWARE_VERSION (version.h) in /sys, /api and every log file.
After every flash it adds a line to logs/flash_history.csv (in bb1-private):
when, version, build id, USB or Wi-Fi, and how it went.
The first OTA-capable firmware goes on over USB once: it changes the flash
layout (Flash Size "Sketch: 1MB, FS: 1MB", see FQBN). After that, either way works.

Options:
  --execute, --yes compile and upload (without this, print the JSON plan)
  --skip-compile   upload the last build (build/bb1) without compiling
  --force          skip the lying-down check; requires --execute
  -p PORT          use this port instead of finding it
  --ota            over Wi-Fi instead of USB
  --host HOST      BB1's address for --ota (default $BB1_HOST or bb1.local)
  --json           with --execute, the final result is JSON (progress on stderr).
                   The plan is always JSON, with or without --json.

Exit codes: 0 ok (including a plan), 1 compile, upload, or safety check failed,
2 usage or configuration (--force without --execute, missing secrets.h, no
build yet, host not found, BB1 didn't answer).

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

import cli_util
import gen_docs
from serial_log import find_port   # picks the Pico by its USB ID

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "firmware" / "bb1"
BUILD = REPO / "build" / "bb1"
# flash=...: 1 MB for the program, 1 MB for LittleFS, where an OTA update waits
# before the bootloader copies it in (ota.h). Same in the CI build.
FQBN = "rp2040:rp2040:rpipicow:flash=2097152_1048576"
LYING_DOWN_DEG = 45        # same as FALL_ANGLE in config.h

# Shown in the plan, and again on stderr, whenever --force is set.
FORCE_WARNING = (
    "--force skips the lying-down safety check. The wheels must be off the table. "
    "--force does nothing unless you also pass --execute."
)

# With --json (and always for the plan) progress goes to stderr so stdout stays one JSON document.
_JSON = False


def say(*args, **kwargs):
    """Progress for a person. With --json this goes to stderr."""
    kwargs["file"] = sys.stderr if _JSON else sys.stdout
    print(*args, **kwargs)


def die(message, code=cli_util.EXIT_FAIL):
    """Stop. Human text on stderr, or one JSON document on stdout with --json."""
    if _JSON:
        print(json.dumps({"ok": False, "error": message, "exit_code": code}))
    else:
        print(message, file=sys.stderr)
    raise SystemExit(code)


def step(text):
    say(f"\n== {text}")


def git(*args):
    return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True).stdout.strip()


def write_build_id():
    """build_id.h: which source this build came from. It changes every time, so
    arduino-cli always recompiles the sketch (and never reuses an old build)."""
    dirty = "+" if git("status", "--porcelain", "--", "firmware") else ""
    build_id = f"{git('branch', '--show-current') or 'detached'} {git('rev-parse', '--short', 'HEAD')}{dirty} " \
               f"{time.strftime('%Y-%m-%d %H:%M')}"
    (SKETCH / "build_id.h").write_text(
        "// Written by tools/flash.py before each compile (see version.h). Not in git.\n"
        f'#define BB1_BUILD_ID "{build_id}"\n')
    return build_id


def last_build_id():
    """The build id of the last compile (for --skip-compile)."""
    try:
        return re.search(r'BB1_BUILD_ID "([^"]*)"', (SKETCH / "build_id.h").read_text()).group(1)
    except (OSError, AttributeError):
        return "no build id"


def firmware_version():
    return re.search(r'FIRMWARE_VERSION = "([^"]*)"', (SKETCH / "version.h").read_text()).group(1)


def record_flash(build_id, method, result):
    """One line per flash in logs/flash_history.csv: what went on BB1, when, and how."""
    history = REPO / "logs" / "flash_history.csv"
    if not history.parent.is_dir():
        say(f"(no logs folder here, so not recorded in {history.name})")
        return
    new = not history.exists()
    with history.open("a") as f:
        if new:
            f.write("time,version,build_id,method,result\n")
        f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')},{firmware_version()},{build_id},{method},{result}\n")
    say(f"Recorded in logs/{history.name}: version {firmware_version()}, {build_id}, {method}, {result}")


def compile_firmware():
    step("Compiling firmware/bb1")
    if not (SKETCH / "secrets.h").exists():
        die("firmware/bb1/secrets.h is missing: copy secrets.example.h to secrets.h first.", cli_util.EXIT_USAGE)
    build_id = write_build_id()
    say(f"version {firmware_version()}, build id {build_id}")
    # The docs BB1 serves (GET /agent, /api.md) come from docs/*.md: rebuild docs_pages.h so they can't be stale.
    try:
        say(gen_docs.write())
    except ValueError as e:
        die(f"tools/gen_docs.py: {e}. Nothing was flashed.")
    result = subprocess.run(["arduino-cli", "compile", "--fqbn", FQBN, "--output-dir", str(BUILD), str(SKETCH)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        say(result.stdout[-3000:], result.stderr[-3000:])
        die("Compile failed: nothing was flashed.")
    for line in result.stdout.splitlines():
        if line.startswith(("Sketch uses", "Global variables")):
            say(line)
    return build_id


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
        die("Couldn't read the pitch from BB1 (is something else holding the port?). "
            "Lay it down and use --force to flash anyway.")
    pitch, trim = pitches[-1], trims[-1] if trims else 0.0
    say(f"pitch {pitch:+.1f} deg, trim {trim:+.1f} deg")
    if abs(pitch - trim) <= LYING_DOWN_DEG:
        die("BB1 is upright: after the reboot it would start balancing on the spot. "
            "Lay it down (on its front) and run this again.")
    say("Lying down: safe to flash.")


def upload(port):
    step(f"Uploading to {port}")
    result = subprocess.run(["arduino-cli", "upload", "-p", port, "--fqbn", FQBN, "--input-dir", str(BUILD), str(SKETCH)],
                            capture_output=True, text=True)
    say((result.stdout + result.stderr).strip()[-1500:])
    if result.returncode != 0:
        die("Upload failed. Close any serial monitor and try again, or use BOOTSEL (docs/FLASHING.md).")


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
        die("BB1 didn't come back on USB. Unplug and replug it, then check with a serial monitor.")
    time.sleep(1.5)
    lines = read_lines(port, 6, "gains")
    wanted = ("auto-arm", "gains v", "Balance settings", "Gyro", "! ")
    for line in lines:
        if line.startswith(wanted):
            say(line)
    if any("auto-arm: waiting" in l for l in lines):
        say("\nDone: flashed, lying down and waiting. Unplug USB to use it.")
    else:
        say("\nFlashed. I didn't see 'auto-arm: waiting' yet: check the lines above.")


# ---------- over Wi-Fi (--ota) ----------

def ota_password():
    m = re.search(r'#define\s+BB1_OTA_PASSWORD\s+"([^"]*)"', (SKETCH / "secrets.h").read_text())
    if not m:
        die("BB1_OTA_PASSWORD is missing from firmware/bb1/secrets.h (see secrets.example.h).", cli_util.EXIT_USAGE)
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
        die(f"BB1 didn't answer at {base}: {e}. Try --host <ip> ('wifi' on serial shows it), or flash over USB.",
            cli_util.EXIT_USAGE)
    if "fs_total" not in sys_info:
        die("BB1's firmware has no OTA yet: flash this one over USB once (without --ota).")
    pitch, trim = s["pitch"], s["gains"]["trim"]
    say(f"pitch {pitch:+.1f} deg, trim {trim:+.1f} deg, armed {s['armed']}, "
        f"now running version {sys_info.get('version', '?')} ({sys_info.get('build_id', sys_info['build'])})")
    if s["armed"] or s["test_running"]:
        die("BB1 is balancing or running a motor test: stop it and lay it down first.")
    if abs(pitch - trim) <= LYING_DOWN_DEG:
        die("BB1 is upright: after the restart it would start balancing on the spot. "
            "Lay it down (on its front) and run this again.")
    say("Lying down: safe to flash.")


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
        die(f"The upload broke off ({e}). BB1 keeps its old firmware: run this again.")
    say(answer, f"({time.time() - t0:.1f} s)")
    if not answer.get("ok"):
        die("BB1 refused or the check failed: it keeps its old firmware.")


def ota_show_status(base, build_id):
    """Wait for BB1 to come back running this build. Returns True if it did."""
    step("BB1 after the restart")
    end = time.time() + 60                     # copying 0.5 MB and joining Wi-Fi again takes a while
    running = None
    while time.time() < end:
        time.sleep(2)
        try:
            running = http_json(f"{base}/sys", timeout=2).get("build_id")
        except (OSError, ValueError):
            continue                           # still restarting
        if running == build_id:
            break
    else:
        say(f"BB1 isn't running build {build_id} after 60 s (it says: {running}). "
            "Check it over USB (serial), or flash over USB. The old firmware still runs if the copy never started.")
        return False
    s = http_json(f"{base}/state")
    g = s["gains"]
    say(f"version {firmware_version()}, build {build_id}, uptime {s['uptime_s']} s, auto-arm: {s['auto_state']}")
    say(f"gains v{g['ver']}: kp {g['kp']:g} ki {g['ki']:g} kd {g['kd']:g} trim {g['trim']:g}")
    say("\nDone: flashed over Wi-Fi.")
    return True


def resolve(host):
    """Look bb1.local up once (.local lookups can be slow, and every request would pay it)."""
    name, _, port = host.partition(":")
    try:
        return f"http://{socket.gethostbyname(name)}{':' + port if port else ''}"
    except OSError:
        die(f"Can't find {name}: use --host <ip> ('wifi' on serial shows it).", cli_util.EXIT_USAGE)


def build_plan(args):
    """What --execute would do. No compile, no network, no serial port."""
    transport = "ota" if args.ota else "usb"
    artifact = "build/bb1/bb1.ino.bin" if args.ota else "build/bb1/bb1.ino.uf2"
    return {
        "ok": True,
        "plan": True,
        "execute": False,
        "action": "flash",
        "target": "firmware/bb1",
        "transport": transport,
        "host": args.host if args.ota else None,
        "port": args.port,
        "firmware_path": "firmware/bb1",
        "artifact": artifact,
        "lying_down_check": not args.force,
        "skip_compile": bool(args.skip_compile),
        "warnings": [FORCE_WARNING] if args.force else [],
    }


def parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skip-compile", action="store_true", help="upload the last build without compiling")
    ap.add_argument("--force", action="store_true",
                    help="skip the lying-down check (wheels off the table); requires --execute")
    ap.add_argument("-p", "--port", help="serial port (default: the Pico's)")
    ap.add_argument("--ota", action="store_true", help="over Wi-Fi instead of USB")
    ap.add_argument("--host", default=os.environ.get("BB1_HOST", "bb1.local"),
                    help="BB1's address for --ota (default $BB1_HOST or bb1.local)")
    ap.add_argument("--execute", "--yes", action="store_true",
                    help="compile and upload. Without this, print a JSON plan and do nothing")
    ap.add_argument("--json", action="store_true",
                    help="with --execute, the result is JSON and progress goes to stderr (a plan is always JSON)")
    cli_util.standard_cli(ap)
    return ap


def finish_json(args, build_id, method):
    """With --execute --json, one result document after the human progress (which went to stderr)."""
    if not _JSON:
        return
    print(json.dumps({
        "ok": True,
        "plan": False,
        "execute": True,
        "action": "flash",
        "target": "firmware/bb1",
        "transport": "ota" if args.ota else "usb",
        "host": args.host if args.ota else None,
        "port": args.port,
        "firmware_path": "firmware/bb1",
        "method": method,
        "build_id": build_id,
        "lying_down_check": not args.force,
        "warnings": [FORCE_WARNING] if args.force else [],
    }))


def main(argv=None):
    global _JSON
    args = parser().parse_args(argv)
    _JSON = bool(args.json)

    if not args.execute:
        plan = build_plan(args)
        if args.force:
            # --force skips the safety check, so it is not something a plan can
            # quietly include. The plan still says what it would skip.
            plan["ok"] = False
            plan["exit_code"] = cli_util.EXIT_USAGE
            plan["error"] = (
                "--force requires --execute: it skips the lying-down safety check, "
                "so it is not applied from a plan"
            )
            print(json.dumps(plan))
            raise SystemExit(cli_util.EXIT_USAGE)
        print(json.dumps(plan))
        raise SystemExit(cli_util.EXIT_OK)

    if args.force:
        say("WARNING: --force skips the lying-down safety check. Wheels must be off the table.")

    if not args.skip_compile:
        build_id = compile_firmware()
    elif not any(BUILD.glob("*.uf2")):
        die("No build in build/bb1 yet: run without --skip-compile.", cli_util.EXIT_USAGE)
    else:
        build_id = last_build_id()
    if args.ota:
        base = resolve(args.host)
        if not args.force:
            ota_check(base)
        ota_upload(base)
        ok = ota_show_status(base, build_id)
        record_flash(build_id, "wifi", "ok" if ok else "not confirmed")
        if not ok:
            if _JSON:
                die("BB1 didn't come back running this build within 60 s.", cli_util.EXIT_FAIL)
            raise SystemExit(cli_util.EXIT_FAIL)
        finish_json(args, build_id, "wifi")
        return
    try:
        port = args.port or find_port()
    except SystemExit as e:
        message = e.code if isinstance(e.code, str) else "No Pico found (is USB plugged in?)."
        die(message, cli_util.EXIT_USAGE)
    if not args.force:
        check_lying_down(port)
    upload(port)
    record_flash(build_id, "usb", "uploaded")
    show_status()
    finish_json(args, build_id, "usb")


if __name__ == "__main__":
    main()
