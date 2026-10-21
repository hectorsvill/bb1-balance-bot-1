"""mock_bb1.py - a pretend BB1 on your Mac, for working on the web page without the robot.

Usage (from the repo folder):
    tools/venv/bin/python tools/mock_bb1.py               # then open http://localhost:8080
    tools/venv/bin/python tools/mock_bb1.py --port 9000

It serves the real page straight out of firmware/bb1/page.h (edit page.h,
reload the browser, no compile or upload needed) and answers the same
endpoints as web.h with made-up data: the robot slowly rocks forward and
back, with a little sensor noise. Record, Save last 10 s, the live log and
tools/logger.py all work against it:
    tools/venv/bin/python tools/logger.py --host localhost:8080 -t 5

Mock logs say "firmware,bb1 MOCK" at the top, so they can never be mistaken
for real robot data (important once logs are used to train a model).

Only the Python standard library is used. Stop it with Ctrl-C.
"""

import argparse
import math
import random
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

REPO = Path(__file__).resolve().parent.parent
PAGE_H = REPO / "firmware" / "bb1" / "page.h"

# Same numbers as the firmware (config.h, imu.h, web.h).
LOOP_HZ = 200
LOG_BUFFER_RECORDS = 10 * LOOP_HZ
LOG_MAX_PER_REQUEST = 1000
ACCEL_PER_G = 16384.0
GYRO_PER_DPS = 65.5
CSV_HEADER = "tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,pitch_rate,accel_pitch"

START = time.time()
alpha = 0.98
bias = [-0.50, 2.07, -2.20]      # deg/s, like a real calibration


def read_page():
    """The page exactly as the firmware sends it: the raw string after PAGE_HTML[] = R"PAGE(."""
    text = PAGE_H.read_text()
    return re.search(r'PAGE_HTML\[\] = R"PAGE\((.*)\)PAGE"', text, re.S).group(1)


def total_written():
    """How many records the pretend robot has made since it 'booted'."""
    return int((time.time() - START) * LOOP_HZ)


def record(tick):
    """Make up record number `tick`. Always the same for the same tick, like a real buffer."""
    rng = random.Random(tick)
    t = tick / LOOP_HZ
    pitch = 20 * math.sin(2 * math.pi * t / 6) + 5 * math.sin(2 * math.pi * t / 1.7)   # rocking
    rate = (20 * 2 * math.pi / 6) * math.cos(2 * math.pi * t / 6) + (5 * 2 * math.pi / 1.7) * math.cos(2 * math.pi * t / 1.7)
    accel_pitch = pitch + rng.gauss(0, 0.8)                                            # accel is noisy
    rad = math.radians(accel_pitch)
    ax, ay, az = rng.gauss(0, 150), math.sin(rad) * ACCEL_PER_G * 0.94, math.cos(rad) * ACCEL_PER_G * 0.94
    gx = (rate + bias[0]) * GYRO_PER_DPS + rng.gauss(0, 10)                            # raw gyro includes the bias
    gy, gz = bias[1] * GYRO_PER_DPS + rng.gauss(0, 10), bias[2] * GYRO_PER_DPS + rng.gauss(0, 10)
    return {
        "tick": tick, "t_us": 2_000_000 + tick * 5000, "work_us": 450 + rng.randint(0, 40), "mode": 2, "flag_bits": 0,
        "raw": [int(ax), int(ay), int(az), -1200 + rng.randint(-5, 5), int(gx), int(gy), int(gz)],
        "pitch": pitch, "pitch_rate": rate, "accel_pitch": accel_pitch,
    }


def csv_line(r):
    raw = ",".join(str(v) for v in r["raw"])
    return (f'{r["tick"]},{r["t_us"]},{r["work_us"]},{r["mode"]},{r["flag_bits"]},{raw},'
            f'{r["pitch"]:.3f},{r["pitch_rate"]:.3f},{r["accel_pitch"]:.3f}\n')


def meta_lines():
    return ("# firmware,bb1 MOCK (tools/mock_bb1.py)\n"
            "# log_format,1\n"
            f"# gyro_bias_dps,{bias[0]:.3f},{bias[1]:.3f},{bias[2]:.3f}\n"
            f"# alpha,{alpha:.3f},loop_hz,{LOOP_HZ},accel_lsb_per_g,{ACCEL_PER_G:.1f},gyro_lsb_per_dps,{GYRO_PER_DPS:.1f}\n"
            "# accel_pitch_sign,1,gyro_pitch_sign,1\n")


class Handler(BaseHTTPRequestHandler):
    def reply(self, body, kind="text/plain", status=200):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        args = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path == "/":
            self.reply(read_page(), "text/html")
        elif url.path == "/state":
            r = record(total_written() - 1)
            self.reply(
                '{"mock":true,"have":true,"tick":%d,"t_us":%d,"mode":2,"flag_bits":0,"pitch":%.2f,"accel_pitch":%.2f,'
                '"pitch_rate":%.2f,"work_us":%d,"alpha":%.3f,"bias":[%.2f,%.2f,%.2f],"calibrations":1,"cal_ok":true,'
                '"ever_cal":true,"imu":true,"wifi_mode":"own","wifi_ssid":"MOCK","ip":"localhost","uptime_s":%d}'
                % (r["tick"], r["t_us"], r["pitch"], r["accel_pitch"], r["pitch_rate"], r["work_us"], alpha,
                   *bias, time.time() - START), "application/json")
        elif url.path == "/log/meta":
            self.reply(meta_lines())
        elif url.path == "/log":
            self.reply(self.log(int(args.get("since", 0))), "text/csv")
        elif url.path == "/sys":
            self.reply('{"ram_total":262144,"ram_static":149000,"heap_total":113144,"heap_used":%d,'
                       '"log_buffer":80000,"flash_total":2097152,"flash_used":421000,"page":%d,"cpu_mhz":133}'
                       % (21000 + random.randint(0, 3000), len(read_page()) + 1), "application/json")
        elif url.path == "/wifi":
            self.reply('{"mode":"own","saved_ssid":"","note":"mock"}', "application/json")
        else:
            self.reply("not found", status=404)

    def log(self, since):
        """Same rules as handleLog in firmware/bb1/web.h."""
        total = total_written()
        oldest = max(0, total - LOG_BUFFER_RECORDS)
        restarted = since > total
        if restarted:
            since = oldest
        first = max(since, oldest)
        lost = oldest - since if 0 < since < oldest else 0
        last = min(total, first + LOG_MAX_PER_REQUEST)
        out = [f"# from,{first},to,{last},lost,{lost}{',restarted,1' if restarted else ''}\n{CSV_HEADER}\n"]
        out += [csv_line(record(n)) for n in range(first, last)]
        return "".join(out)

    def do_POST(self):
        global alpha
        length = int(self.headers.get("Content-Length") or 0)
        form = {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode()).items()}
        if self.path == "/cal":
            self.reply("calibrating, keep the robot still for 2 s (mock)")
        elif self.path == "/alpha":
            a = float(form.get("value") or 0)
            if not 0.90 <= a <= 0.999:
                self.reply("alpha must be between 0.90 and 0.999", status=400)
            else:
                alpha = a
                self.reply(f"alpha = {a:.3f}")
        elif self.path in ("/wifi", "/wifi/forget"):
            self.reply("mock: nothing saved, the real robot would restart now")
        else:
            self.reply("not found", status=404)

    def log_message(self, fmt, *a):
        pass          # keep the terminal quiet; the page polls 20 times a second


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080)
    args = ap.parse_args()
    server = ThreadingHTTPServer(("localhost", args.port), Handler)
    print(f"Mock BB1 running: open http://localhost:{args.port}  (Ctrl-C to stop)")
    print(f"Serving the page from {PAGE_H.relative_to(REPO)}; edit it and reload the browser.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
