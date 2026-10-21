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
import json
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
CSV_HEADER = ("tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,pitch_rate,accel_pitch,"
              "enc_l,enc_r,speed_l,speed_r,pwm_l,pwm_r,setpoint,p_term,i_term,d_term,gains_ver,reason")
MOTOR_TEST_MAX_PWM = 150
MOTOR_TEST_MAX_MS = 3000
COUNTS_PER_S_PER_PWM = 17.5      # measured: about 1750 counts/s at PWM 100, wheels in the air

START = time.time()
alpha = 0.98
bias = [-0.50, 2.07, -2.20]      # deg/s, like a real calibration
tests = []                        # motor tests: [first_tick, end_tick, left_pwm, right_pwm]
gains = {"ver": 1, "kp": 15.0, "ki": 0.0, "kd": 0.5, "trim": 0.0, "cap": 150.0, "deadband": 0.0}
armed_from = None                 # tick it was armed at, or None
reason = ""                       # why it last disarmed / refused, like disarmReason in control.h
arm_attempts = 0
REASONS = {"disarmed by you": 1, "not upright: hold it near the balance point to arm": 6,
           "the page was hidden (tab switched or screen locked)": 10}   # codes as in control.h


def test_at(tick):
    """The motor test running at this tick, or None."""
    for t in tests:
        if t[0] <= tick < t[1]:
            return t
    return None


def encoders(tick):
    """Pretend encoder counts: each test adds pwm x 17.5 counts/s while it runs."""
    left = right = 0.0
    for first, end, l, r in tests:
        n = max(0, min(tick, end) - first)            # ticks of this test before `tick`
        left += n * l * COUNTS_PER_S_PER_PWM / LOOP_HZ
        right += n * r * COUNTS_PER_S_PER_PWM / LOOP_HZ
    return int(left), int(right)


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
    test = test_at(tick)
    enc_l, enc_r = encoders(tick)
    old_l, old_r = encoders(max(0, tick - 10))       # speed over the last 10 ticks, like motors.h
    r = {
        "tick": tick, "t_us": 2_000_000 + tick * 5000, "work_us": 450 + rng.randint(0, 40),
        "mode": 3 if test else 2, "flag_bits": 0,
        "enc": [enc_l, enc_r], "speed": [(enc_l - old_l) * 20, (enc_r - old_r) * 20],
        "pwm": [test[2], test[3]] if test else [0, 0], "ctl": [gains["trim"], 0, 0, 0],
        "raw": [int(ax), int(ay), int(az), -1200 + rng.randint(-5, 5), int(gx), int(gy), int(gz)],
        "pitch": pitch, "pitch_rate": rate, "accel_pitch": accel_pitch,
    }
    if armed_from is not None and tick >= armed_from and not test:
        # Pretend PD loop, same formula as control.h. (The mock robot keeps rocking
        # whatever the PWM says: it shows the numbers, it doesn't simulate physics.)
        g = gains
        p, d = g["kp"] * (pitch - g["trim"]), g["kd"] * rate
        out = max(-g["cap"], min(g["cap"], p + d))
        r.update(mode=4, flag_bits=8 if abs(p + d) >= g["cap"] else 0,
                 pwm=[int(round(out))] * 2, ctl=[g["trim"], p, 0.0, d])
    return r


def csv_line(r):
    raw = ",".join(str(v) for v in r["raw"])
    return (f'{r["tick"]},{r["t_us"]},{r["work_us"]},{r["mode"]},{r["flag_bits"]},{raw},'
            f'{r["pitch"]:.3f},{r["pitch_rate"]:.3f},{r["accel_pitch"]:.3f},'
            f'{r["enc"][0]},{r["enc"][1]},{r["speed"][0]},{r["speed"][1]},{r["pwm"][0]},{r["pwm"][1]},'
            f'{r["ctl"][0]:.2f},{r["ctl"][1]:.1f},{r["ctl"][2]:.1f},{r["ctl"][3]:.1f},{gains["ver"]},{REASONS.get(reason, 0)}\n')


def meta_lines():
    return ("# firmware,bb1 MOCK (tools/mock_bb1.py)\n"
            "# log_format,4\n"
            f"# gyro_bias_dps,{bias[0]:.3f},{bias[1]:.3f},{bias[2]:.3f}\n"
            f"# alpha,{alpha:.3f},loop_hz,{LOOP_HZ},accel_lsb_per_g,{ACCEL_PER_G:.1f},gyro_lsb_per_dps,{GYRO_PER_DPS:.1f}\n"
            "# accel_pitch_sign,1,gyro_pitch_sign,1\n"
            "# counts_per_rev,520,speed_window_ticks,10,pwm_max,255,pwm_freq_hz,20000\n"
            "# gains_ver,{ver},kp,{kp:.3f},ki,{ki:.3f},kd,{kd:.3f},trim,{trim:.2f},pwm_cap,{cap:.0f},deadband,{deadband:.0f}\n"
            .format(**gains))


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
            done = [t for t in tests if t[1] <= total_written()]
            last = done[-1] if done else [0, 0, 0, 0]
            moved = [int((last[1] - last[0]) * pwm * COUNTS_PER_S_PER_PWM / LOOP_HZ) for pwm in last[2:]]
            state = {
                "mock": True, "have": True, "tick": r["tick"], "t_us": r["t_us"], "mode": r["mode"], "flag_bits": 0,
                "pitch": round(r["pitch"], 2), "accel_pitch": round(r["accel_pitch"], 2),
                "pitch_rate": round(r["pitch_rate"], 2), "work_us": r["work_us"], "alpha": alpha, "bias": bias,
                "calibrations": 1, "cal_ok": True, "ever_cal": True, "imu": True,
                "wifi_mode": "own", "wifi_ssid": "MOCK", "ip": "localhost", "uptime_s": int(time.time() - START),
                "enc_l": r["enc"][0], "enc_r": r["enc"][1], "speed_l": r["speed"][0], "speed_r": r["speed"][1],
                "pwm_l": r["pwm"][0], "pwm_r": r["pwm"][1], "test_running": r["mode"] == 3,
                "tests_done": len(done), "test_moved": moved, "test_ms": (last[1] - last[0]) * 5,
                "armed": r["mode"] == 4, "reason": reason, "arm_attempts": arm_attempts,
                "p": round(r["ctl"][1], 1), "i": 0.0, "d": round(r["ctl"][3], 1), "gains": gains,
            }
            self.reply(json.dumps(state), "application/json")
        elif url.path == "/log/meta":
            self.reply(meta_lines())
        elif url.path == "/log":
            self.reply(self.log(int(args.get("since", 0))), "text/csv")
        elif url.path == "/sys":
            self.reply('{"ram_total":262144,"ram_static":181200,"heap_total":80944,"heap_used":%d,'
                       '"log_buffer":112000,"flash_total":2097152,"flash_used":433000,"page":%d,"cpu_mhz":133}'
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
        global alpha, armed_from, reason, arm_attempts
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
        elif self.path == "/motor":
            now = total_written()
            if armed_from is not None:
                return self.reply("disarm first: no motor test while balancing", status=409)
            if test_at(now):
                return self.reply("a motor test is already running", status=409)
            clamp = lambda v, lim: max(-lim, min(lim, int(v or 0)))
            l, r = clamp(form.get("left"), MOTOR_TEST_MAX_PWM), clamp(form.get("right"), MOTOR_TEST_MAX_PWM)
            ms = max(0, min(MOTOR_TEST_MAX_MS, int(form.get("ms") or 2000)))
            tests.append([now, now + ms // 5, l, r])
            self.reply(f"running: left {l}, right {r} for {ms} ms (mock: nothing moves)")
        elif self.path == "/arm":
            r = record(total_written() - 1)
            arm_attempts += 1
            if abs(r["pitch"] - gains["trim"]) > 10:          # same rule as balanceArm() in control.h
                reason = "not upright: hold it near the balance point to arm"
            else:
                armed_from, reason = total_written(), ""
            self.reply("arming...")
        elif self.path.startswith("/disarm"):
            if armed_from is not None:
                hidden = "why=hidden" in self.path
                armed_from = None
                reason = "the page was hidden (tab switched or screen locked)" if hidden else "disarmed by you"
            self.reply("disarmed")
        elif self.path == "/gains":
            limits = {"kp": (0, 100), "ki": (0, 100), "kd": (0, 20), "cap": (0, 255), "deadband": (0, 100), "trim": (-20, 20)}
            for k, (lo, hi) in limits.items():
                v = form.get(k)
                if k == "trim" and v == "here":
                    gains["trim"] = round(record(total_written() - 1)["pitch"], 2)
                elif v:
                    gains[k] = max(lo, min(hi, float(v)))
            gains["ver"] += 1
            self.reply("gains v{ver}: kp {kp}, ki {ki}, kd {kd}, trim {trim}, cap {cap}, deadband {deadband}".format(**gains))
        elif self.path == "/motor/stop":
            if armed_from is not None:
                armed_from, reason = None, "disarmed by you"
            now = total_written()
            for t in tests:
                if t[0] <= now < t[1]:
                    t[1] = now
            self.reply("stopped")
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
