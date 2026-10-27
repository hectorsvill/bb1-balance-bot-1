"""logger.py - record every 5 ms log record from BB1 over Wi-Fi into a CSV file.

Usage (from the repo folder, venv active or not):
    tools/venv/bin/python tools/logger.py                         # until Ctrl-C
    tools/venv/bin/python tools/logger.py -t 60 -n "tilt test"    # 60 s, with a note
    tools/venv/bin/python tools/logger.py --host 192.168.42.1     # on BB1's own network
    tools/venv/bin/python tools/logger.py --host localhost:8080   # the mock robot (tools/mock_bb1.py)

Writes logs/bb1_<date>_<time>[_note].csv: the same layout as the web page's
Download and the serial "dump" (settings lines, header, one row per tick), so
tools/check_dump.py reads it. Column meanings: docs/CALIBRATION_AND_LOGS.md.

How it works: the robot keeps the last 10 s of records. Four times a second we
ask GET /log?since=<next tick we haven't got>, append what comes back, and
remember where it ended. If we ever fall more than 10 s behind, the robot says
how many records were lost and we count them.
"""

import argparse
import json
import re
import socket
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAX_PER_REQUEST = 1000          # must match LOG_MAX_PER_REQUEST in firmware/bb1/web.h


def get(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as res:
        return res.read().decode()


def post(url, timeout=3, **fields):
    """POST form fields (like the web page's buttons do) and return BB1's answer text."""
    data = urllib.parse.urlencode(fields).encode()
    with urllib.request.urlopen(url, data=data, timeout=timeout) as res:
        return res.read().decode()


def fetch_log(base, since):
    """Returns (info dict, header line, list of row lines)."""
    lines = [l for l in get(f"{base}/log?since={since}").split("\n") if l]
    parts = lines[0][2:].split(",")                    # "# from,1200,to,1450,lost,0"
    info = {k: int(v) for k, v in zip(parts[0::2], parts[1::2])}
    return info, lines[1], lines[2:]


def log_path(note, started, out=None):
    """logs/bb1_<date>_<time>[_note].csv inside the repo (or the given path)."""
    slug = re.sub(r"[^a-z0-9]+", "-", note.lower()).strip("-")[:40]
    out = Path(out) if out else Path("logs") / f"bb1_{started:%Y-%m-%d_%H-%M-%S}{'_' + slug if slug else ''}.csv"
    if not out.is_absolute():
        out = REPO / out
    out.parent.mkdir(parents=True, exist_ok=True)
    return out


# Every setting POST /gains takes, in the order the "# gains_vN" lines list them.
# handheld.py uses the same list: keep one copy so a new setting reaches every log.
GAIN_KEYS = ("kp", "ki", "kd", "trim", "cap", "deadband", "sp", "si", "th", "auto", "dff", "sdi", "smode", "wsp", "wsi")


def gains_versions(state_json, versions):
    """Remember what each gains version meant, from a /state answer.

    Rows only carry gains_ver (a number), so the file needs one
    "# gains_vN,kp ... cap ..." line per version used during the run, or a
    model trained on it later can't tell which gains produced which rows."""
    g = json.loads(state_json)["gains"]
    versions[g["ver"]] = " ".join(f"{k} {g[k]:g}" for k in GAIN_KEYS if k in g)


def save_log(out, meta, note, started, header, rows, lost, versions=None, surface="unknown"):
    """Write the same layout as the page's Download: settings lines, header, rows.

    surface (wood, carpet, air...) is a label for analysis and training: the
    same gains behave differently on different floors."""
    with open(out, "w") as f:
        f.write(meta)
        f.writelines(f"# gains_v{v},{g}\n" for v, g in sorted((versions or {}).items()))
        f.write(f"# surface,{surface.replace(',', ';')}\n")
        f.write(f"# note,{note.replace(',', ';')}\n")
        f.write(f"# recorded_at,{started.astimezone().isoformat()}\n")
        f.write(f"# records,{len(rows)},lost,{lost}\n")
        f.write(header + "\n")
        f.write("\n".join(rows) + "\n")
    print(f"Saved {len(rows)} records ({len(rows) / 200:.1f} s, {lost} lost) to {out}")
    print(f"Check it:  tools/venv/bin/python tools/check_dump.py {out.relative_to(REPO) if out.is_relative_to(REPO) else out} --plot")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="bb1.local", help="robot address, optionally with :port (default bb1.local)")
    ap.add_argument("-t", "--seconds", type=float, help="stop after this many seconds (default: Ctrl-C)")
    ap.add_argument("-n", "--note", default="", help="a note saved in the file and its name")
    ap.add_argument("-o", "--out", help="output file (default logs/bb1_<date>_<time>[_note].csv)")
    ap.add_argument("--surface", default="unknown", help="what BB1 stands on: wood, carpet, air... (saved in the file)")
    args = ap.parse_args()

    # Look up bb1.local once. Some tools take seconds to resolve .local names
    # on every request; the IP address is instant.
    name, _, port = args.host.partition(":")
    try:
        ip = socket.gethostbyname(name)
    except OSError:
        sys.exit(f"Can't find {name}. Is BB1 on and on this network? Try --host <ip> (type 'wifi' on serial).")
    base = f"http://{ip}{':' + port if port else ''}"

    started = datetime.now()
    out = log_path(args.note, started, args.out)

    meta = get(f"{base}/log/meta")
    state = get(f"{base}/state")
    next_tick = json.loads(state)["tick"] + 1                         # start from "now"
    versions = {}
    gains_versions(state, versions)

    rows, lost, header = [], 0, ""
    print(f"Recording from {args.host} ({ip}) into {out}")
    print("Press Ctrl-C to stop." if not args.seconds else f"Stopping after {args.seconds:.0f} s.")
    t0 = time.time()
    last_print = 0
    try:
        while not args.seconds or time.time() - t0 < args.seconds:
            try:
                info, header, new = fetch_log(base, next_tick)
            except OSError as e:
                print(f"  no answer from the robot ({e}), retrying")
                time.sleep(0.5)
                continue
            rows += new
            lost += info["lost"]
            next_tick = info["to"]
            if time.time() - last_print > 2:
                try:
                    gains_versions(get(f"{base}/state"), versions)   # catch gains changed on the page
                except OSError:
                    pass
                last_print = time.time()
                print(f"  {len(rows)} records ({len(rows) / 200:.1f} s){f', {lost} lost' if lost else ''}")
            if info["to"] - info["from"] < MAX_PER_REQUEST:
                time.sleep(0.25)          # caught up: wait a little before asking again
    except KeyboardInterrupt:
        pass

    save_log(out, meta, args.note, started, header, rows, lost, versions, args.surface)


if __name__ == "__main__":
    main()
