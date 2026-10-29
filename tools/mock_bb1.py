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
TUNE_H = REPO / "firmware" / "bb1" / "tune_page.h"

# Same numbers as the firmware (config.h, imu.h, web.h).
LOOP_HZ = 200
LOG_BUFFER_RECORDS = 10 * LOOP_HZ
LOG_MAX_PER_REQUEST = 1000
ACCEL_PER_G = 16384.0
GYRO_PER_DPS = 65.5
CSV_HEADER = ("tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,pitch_rate,accel_pitch,"
              "enc_l,enc_r,speed_l,speed_r,pwm_l,pwm_r,setpoint,p_term,i_term,d_term,gains_ver,reason,web_age_ms,batt_v,auto_state,drive_speed,drive_turn,drive_speed_now,drive_turn_now,speed_pwm")
MOTOR_TEST_MAX_PWM = 150
MOTOR_TEST_MAX_MS = 3000
DRIVE_MAX_SPEED, DRIVE_MAX_TURN = 1.0, 1.0   # config.h: wheel turns/s
DRIVE_HOLD_MS, DRIVE_HOLD_MAX_MS = 500, 3000
COUNTS_PER_S_PER_PWM = 17.5      # measured: about 1750 counts/s at PWM 100, wheels in the air

START = time.time()
alpha = 0.98
bias = [-0.50, 2.07, -2.20]      # deg/s, like a real calibration
tests = []                        # motor tests: [first_tick, end_tick, left_pwm, right_pwm]
gains = {"ver": 1, "kp": 15.0, "ki": 0.0, "kd": 0.5, "trim": -4.39, "cap": 80.0, "deadband": 0.0, "sp": 0.0, "si": 0.0, "th": 0.0, "auto": 1, "dff": 25.0, "sdi": 2.0, "smode": 0, "wsp": 20.0, "wsi": 10.0, "irelax": 0.5, "aff": 0.0, "dacc": 1.0, "lmax": 6.0, "tl": 1.0}
armed_from = None                 # tick it was armed at, or None
reason = ""                       # why it last disarmed / refused, like disarmReason in control.h
arm_attempts = 0
arm_until = None                  # tick the arm timer ends at, or None
drive = {"speed": 0.0, "turn": 0.0, "until": 0.0, "commands": 0}   # like driveSpeedCmd / driveTurnCmd / driveUntilMs (time.time())
REASONS = {"disarmed by you": 1, "not upright: hold it near the balance point to arm": 6,
           "the page was hidden (tab switched or screen locked)": 10, "arm time finished": 11,
           "picked up (wheels spinning in the air)": 12,
           "battery low (under 3.3 V per cell)": 13}   # codes as in control.h


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


def read_page(path=PAGE_H, name="PAGE_HTML"):
    """A page exactly as the firmware sends it: the raw string after NAME[] = R"PAGE(."""
    text = path.read_text()
    return re.search(name + r'\[\] = R"PAGE\((.*)\)PAGE"', text, re.S).group(1)


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


def drive_now():
    """The drive command if it hasn't run out (mock: no ramp), else (0, 0)."""
    # A command never outlives the arm it was given in (balanceArm/balanceDisarm clear it)
    if armed_from is None or drive.get("arm") != armed_from or time.time() >= drive["until"]:
        return 0.0, 0.0
    return drive["speed"], drive["turn"]


def mock_battery(tick):
    """A pretend 2S pack: 8.2 V, sagging a little while the motors run, slowly draining."""
    return round(8.2 - tick / 200 / 3600 * 0.5 - (0.15 if tick % 400 < 40 else 0), 2)


def csv_line(r):
    raw = ",".join(str(v) for v in r["raw"])
    return (f'{r["tick"]},{r["t_us"]},{r["work_us"]},{r["mode"]},{r["flag_bits"]},{raw},'
            f'{r["pitch"]:.3f},{r["pitch_rate"]:.3f},{r["accel_pitch"]:.3f},'
            f'{r["enc"][0]},{r["enc"][1]},{r["speed"][0]},{r["speed"][1]},{r["pwm"][0]},{r["pwm"][1]},'
            f'{r["ctl"][0]:.2f},{r["ctl"][1]:.1f},{r["ctl"][2]:.1f},{r["ctl"][3]:.1f},{gains["ver"]},{REASONS.get(reason, 0)},{r["tick"] % 6 * 10},{mock_battery(r["tick"]):.2f},{6 if r["mode"] == 4 else 1},0.00,0.00,0.00,0.00,0\n')


def meta_lines():
    return ("# firmware,bb1 MOCK (tools/mock_bb1.py)\n"
            "# firmware_version,MOCK,build_id,MOCK\n"
            "# log_format,11\n"
            f"# gyro_bias_dps,{bias[0]:.3f},{bias[1]:.3f},{bias[2]:.3f}\n"
            f"# alpha,{alpha:.3f},loop_hz,{LOOP_HZ},accel_lsb_per_g,{ACCEL_PER_G:.1f},gyro_lsb_per_dps,{GYRO_PER_DPS:.1f}\n"
            "# accel_pitch_sign,1,gyro_pitch_sign,1\n"
            "# counts_per_rev,520,speed_window_ticks,10,pwm_max,255,pwm_freq_hz,20000\n"
            "# battery_divider,12.07,battery_pin,GP26,battery_cells,2\n"
            "# gains_ver,{ver},kp,{kp:.3f},ki,{ki:.3f},kd,{kd:.3f},trim,{trim:.2f},pwm_cap,{cap:.0f},deadband,{deadband:.0f}\n"
            .format(**gains))


# GET /api: the same list as firmware/bb1/api.h (keep the two in step).
GAIN_LIMITS = {"kp": (0, 100), "ki": (0, 100), "kd": (0, 20), "trim": (-20, 20), "cap": (0, 255), "deadband": (0, 100), "sp": (0, 20), "si": (0, 20), "th": (0, 200), "auto": (0, 1), "dff": (0, 150), "sdi": (0, 20), "smode": (0, 1), "wsp": (0, 200), "wsi": (0, 200), "irelax": (0, 5), "aff": (0, 5), "dacc": (0.1, 6), "lmax": (6, 15), "tl": (0, 5)}
API_ENDPOINTS = [   # method, path, args, answer, moves, does
    ("GET", "/api", "", "json", False, "this list"),
    ("GET", "/state", "", "json", False, "everything now; also the heartbeat for heartbeat arms"),
    ("GET", "/log", "since", "csv", False, "every 5 ms record from tick since on (max log_max_per_request)"),
    ("GET", "/log/meta", "", "text", False, "the # setting lines for the top of a log file"),
    ("GET", "/sys", "", "json", False, "RAM and flash use"),
    ("GET", "/wifi", "", "json", False, "network mode and saved home network name"),
    ("POST", "/arm", "seconds,heartbeat", "text", True, "start balancing; refused unless within arm_angle of trim; result in /state armed, reason"),
    ("POST", "/disarm", "why", "text", False, "stop balancing; auto-arm off until the next arm"),
    ("POST", "/drive", "speed,turn,ms", "json", True, "drive while balancing; runs out after ms, then ramps to a stop"),
    ("POST", "/drive/stop", "", "json", False, "ramp to a stop now, keep balancing"),
    ("POST", "/motor/stop", "", "text", False, "STOP: end a motor test and disarm"),
    ("POST", "/step", "", "text", True, "step test while armed: setpoint +2 deg for 1 s"),
    ("POST", "/gains", ",".join(GAIN_LIMITS), "text", False, "change settings (see gains for limits)"),
    ("POST", "/gains/save", "", "text", False, "store settings in flash (disarmed only)"),
    ("POST", "/gains/defaults", "", "text", False, "back to the config.h defaults"),
    ("POST", "/alpha", "value", "text", False, "angle filter weight"),
    ("POST", "/cal", "", "text", False, "recalibrate the gyro (2 s, keep still; disarms)"),
    ("POST", "/motor", "left,right,ms", "text", True, "timed motor test, disarmed only, wheels in the air"),
    ("POST", "/wifi", "ssid,password", "text", False, "save a home network and restart"),
    ("POST", "/wifi/forget", "", "text", False, "forget the home network and restart"),
    ("POST", "/update", "firmware", "json", False,
     "new firmware over Wi-Fi (multipart file, Basic auth bb1); lying down and disarmed only; restarts"),
    ("GET", "/", "", "html", False, "the main page"),
    ("GET", "/tune", "", "html", False, "the PID tuning page"),
]


def api_list():
    return {
        "api": 5, "firmware": "bb1 MOCK", "version": "MOCK", "build_id": "MOCK", "log_format": 11,
        "units": {"angle": "deg (forward +)", "speed": "wheel turns/s (forward +)",
                  "turn": "wheel turns/s left minus right (right +)", "pwm": "-255..255 (forward +)", "counts_per_rev": 520},
        "limits": {"drive_max_speed": DRIVE_MAX_SPEED, "drive_max_turn": DRIVE_MAX_TURN, "drive_hold_ms": DRIVE_HOLD_MS,
                   "drive_hold_max_ms": DRIVE_HOLD_MAX_MS, "arm_seconds_max": 600, "arm_angle": 10.0, "fall_angle": 45.0,
                   "heartbeat_ms": 2000, "motor_test_max_pwm": MOTOR_TEST_MAX_PWM, "motor_test_max_ms": MOTOR_TEST_MAX_MS,
                   "alpha": [0.9, 0.999], "log_max_per_request": LOG_MAX_PER_REQUEST},
        "gains": {k: list(v) for k, v in GAIN_LIMITS.items()},
        "endpoints": [{"method": m, "path": p, "args": a.split(",") if a else [], "answer": ans, "moves": mv, "does": d}
                      for m, p, a, ans, mv, d in API_ENDPOINTS],
    }


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
        elif url.path == "/tune":
            self.reply(read_page(TUNE_H, "TUNE_HTML"), "text/html")
        elif url.path == "/api":
            self.reply(json.dumps(api_list()), "application/json")
        elif url.path == "/state":
            global armed_from, reason
            if armed_from is not None and arm_until and total_written() >= arm_until:   # the arm timer ran out
                armed_from, reason = None, "arm time finished"
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
                "arm_ms": int((arm_until - armed_from) * 5) if arm_until and armed_from is not None else 0,
                "step": False, "auto_state": "balancing" if r["mode"] == 4 else "waiting: stand it up and hold still",
                "arm_left_ms": max(0, (arm_until - total_written()) * 5) if arm_until and armed_from is not None else -1,
                "p": round(r["ctl"][1], 1), "i": 0.0, "d": round(r["ctl"][3], 1), "gains": gains,
                "drive": {"speed": drive_now()[0], "turn": drive_now()[1], "speed_now": drive_now()[0], "turn_now": drive_now()[1],
                          "left_ms": max(0, int((drive["until"] - time.time()) * 1000)) if drive_now() != (0.0, 0.0) else 0,
                          "commands": drive["commands"], "max_speed": DRIVE_MAX_SPEED, "max_turn": DRIVE_MAX_TURN},
                "enc_fault": 0,
                "trim_learned": 0.0 if gains["tl"] <= 0 else 0.42,   # mock: a fixed learned trim (firmware 3.5)
                "batt_v": mock_battery(r["tick"]), "batt_ok": True, "batt_warn_v": 7.0, "batt_low_v": 6.6, "batt_cells": 2,
            }
            self.reply(json.dumps(state), "application/json")
        elif url.path == "/log/meta":
            self.reply(meta_lines())
        elif url.path == "/log":
            self.reply(self.log(int(args.get("since", 0))), "text/csv")
        elif url.path == "/sys":
            self.reply('{"ram_total":262144,"ram_static":181200,"heap_total":80944,"heap_used":%d,'
                       '"log_buffer":144000,"flash_total":2097152,"flash_used":433000,"page":%d,"cpu_mhz":133,'
                       '"fs_total":1048576,"build":"MOCK","version":"MOCK","build_id":"MOCK"}'
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
        global alpha, armed_from, reason, arm_attempts, arm_until
        length = int(self.headers.get("Content-Length") or 0)
        if self.path == "/update":             # firmware upload (ota.h): binary, so before the form parsing
            self.rfile.read(length)
            if armed_from is not None:
                return self.reply('{"ok":false,"error":"balancing: disarm and lay it down first"}', "application/json", 409)
            return self.reply('{"ok":false,"error":"mock: there is no firmware here to replace"}', "application/json", 409)
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
                secs = max(0.0, min(600.0, float(form.get("seconds") or 0)))
                arm_until = armed_from + int(secs * LOOP_HZ) if secs else None
            self.reply("arming...")
        elif self.path.startswith("/disarm"):
            if armed_from is not None:
                hidden = "why=hidden" in self.path
                armed_from = None
                reason = "the page was hidden (tab switched or screen locked)" if hidden else "disarmed by you"
            self.reply("disarmed")
        elif self.path == "/gains":
            for k, (lo, hi) in GAIN_LIMITS.items():
                v = form.get(k)
                if k == "trim" and v == "here":
                    gains["trim"] = round(record(total_written() - 1)["pitch"], 2)
                elif v:
                    gains[k] = max(lo, min(hi, float(v)))
            gains["ver"] += 1
            self.reply("gains v{ver}: kp {kp}  ki {ki}  kd {kd}  trim {trim} deg  cap {cap}  deadband {deadband}  sp {sp}  si {si}  th {th}  auto {auto}  dff {dff:g}  sdi {sdi:g}  smode {smode:g}  wsp {wsp:g}  wsi {wsi:g}  irelax {irelax:g}  aff {aff:g}  dacc {dacc:g}  lmax {lmax:g}  tl {tl:g}".format(**gains))
        elif self.path == "/gains/save":
            if armed_from is not None:
                return self.reply("disarm first: saving pauses the control loop for a moment", status=409)
            self.reply("saved (mock: nothing is really stored)")
        elif self.path == "/gains/defaults":
            gains.update(kp=15.0, ki=0.0, kd=0.5, trim=-4.39, cap=80.0, deadband=0.0)
            gains["ver"] += 1
            self.reply("back to the config.h defaults (press Save to keep them)")
        elif self.path == "/step":
            if armed_from is None:
                return self.reply("arm first: the step test moves the setpoint while balancing", status=409)
            self.reply("step: setpoint +2.0 deg for 1000 ms (mock: not simulated)")
        elif self.path == "/drive":
            # Same rules as handleDrive() in web.h (mock: the wheels don't really move).
            if armed_from is None:
                return self.reply('{"ok":false,"error":"not balancing: arm first (POST /arm)"}', "application/json", 409)
            def num(k, default=0.0):
                # Like Arduino's String.toFloat(): the leading number, 0 if there is none
                m = re.match(r"\s*[-+]?(\d+\.?\d*|\.\d+|nan|inf)", form.get(k) or "", re.I)
                return float(m.group(0)) if m else (default if form.get(k) is None else 0.0)
            speed, turn = num("speed"), num("turn")
            if not (math.isfinite(speed) and math.isfinite(turn)):
                return self.reply('{"ok":false,"error":"speed and turn must be numbers"}', "application/json", 400)
            speed = max(-DRIVE_MAX_SPEED, min(DRIVE_MAX_SPEED, speed))
            turn = max(-DRIVE_MAX_TURN, min(DRIVE_MAX_TURN, turn))
            ms = int(max(0, min(DRIVE_HOLD_MAX_MS, int(num("ms", DRIVE_HOLD_MS)))))
            drive.update(speed=speed, turn=turn, until=time.time() + ms / 1000 if ms else 0.0,
                         commands=drive["commands"] + 1, arm=armed_from)
            self.reply(json.dumps({"ok": True, "speed": speed, "turn": turn, "ms": ms}), "application/json")
        elif self.path == "/drive/stop":
            drive["until"] = 0.0
            self.reply('{"ok":true,"speed":0,"turn":0,"ms":0}', "application/json")
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
