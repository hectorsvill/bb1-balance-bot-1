# BB1 - Balance Bot 1

A two-wheel self-balancing robot, hand-soldered and programmed one small step at a time to learn embedded systems and PID control.

**Status (2026-10-24):** BB1 balances in place on a wood floor: about 0.2 deg of wobble, holds its spot, recovers from taps, and starts balancing by itself when you stand it up. Next: driving it from the web page (milestone 2).

## What's in it

| Part | What it does |
|---|---|
| Raspberry Pi Pico W (RP2040) | Main controller: core 1 runs the 200 Hz control loop, core 0 does Wi-Fi, the web pages and serial |
| D153C dual motor driver (TB6612) | Drives both motors; its built-in divider gives the battery voltage |
| 2x MG310P20 7.4 V gear motors (20:1) with Hall encoders | Wheels, plus wheel speed and distance |
| GY-521 (MPU-6050) | Accelerometer and gyro for the tilt angle |
| WHEELTEC 2-wheel chassis, 2S 1300 mAh LiPo | Base and power |
| 3D-printed reverse-T frame and carriers | Holds the boards; sources in `cad/` |

Wiring: [docs/WIRING.md](docs/WIRING.md) (diagram: [docs/wiring.png](docs/wiring.png)).

## How it balances

```text
IMU -> complementary filter -> pitch
pitch -> angle PID (P 18, I 100, D 0.5) -> PWM to both wheels
wheel speed + distance -> speed loop -> moves the angle target (stop drifting, hold position)
left vs right distance -> heading hold -> steers the wheels apart (stay straight)
```

The surprise: on the floor these geared motors act like a *speed* knob, not a *push* knob, so the angle loop needs a big I term (P and D alone always ran away). The whole tuning story, round by round, is in [docs/TUNING_LOG.md](docs/TUNING_LOG.md); the theory, with FPV / Betaflight comparisons, in [docs/PID_LESSON.md](docs/PID_LESSON.md).

## Web pages (on the Pico)

- `http://bb1.local/tune`: change every gain live, ARM with a timer, STOP, live P / I / D terms, battery per cell.
- `http://bb1.local`: status, ARM / STOP and the **Drive** pad (milestone 2); folded below, everything else (motor test, logs, sensors, Wi-Fi setup).

Details: [docs/WEB_INTERFACE.md](docs/WEB_INTERFACE.md). Everything the pages do is a plain HTTP API, usable from curl or a script: [docs/API.md](docs/API.md). The pages have **no login**: anyone on the same Wi-Fi can arm the robot. Use it on your own network.

## Logging (for tuning now, for training a model later)

Every 5 ms tick is logged: raw IMU, pitch, wheel encoders and speeds, the P / I / D terms, PWM to each motor, the angle target, battery voltage, why it disarmed. `tools/handheld.py` / `tools/logger.py` pull the log over Wi-Fi into a CSV; `tools/pid_report.py` summarizes each try. Columns: [docs/CALIBRATION_AND_LOGS.md](docs/CALIBRATION_AND_LOGS.md). Data rules: [docs/FIRMWARE_PLAN.md](docs/FIRMWARE_PLAN.md), section 6. Recorded logs stay out of git (`logs/`).

## Build and flash

Arduino C++ on the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core (tested with 6.3.0), board "Raspberry Pi Pico W".

1. Make your own secrets file (git ignores it), then set `BB1_AP_PASSWORD` in it:

```bash
cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h
```

2. Compile:

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow:flash=2097152_1048576 --output-dir build/bb1 firmware/bb1
```

3. Upload (find the port with `ls /dev/cu.usbmodem*`):

```bash
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn rp2040:rp2040:rpipicow:flash=2097152_1048576 --input-dir build/bb1 firmware/bb1
```

Or drag `firmware/uf2/bb1.ino.uf2` onto the Pico in BOOTSEL mode (its own-network password is the template's `change-me-please`). Full guide, Arduino IDE steps and troubleshooting: [docs/FLASHING.md](docs/FLASHING.md). Python tools: [docs/TOOLS.md](docs/TOOLS.md).

> **Safety.** With auto-arm on, BB1 starts driving its wheels 1 s after it's held upright and still: lay it down to carry it (a lift detector stops the wheels if they spin up in the air; see [docs/POWER_ON.md](docs/POWER_ON.md)). Every motor run stops by itself (45 deg fall cut-out, arm timer, STOP). **Power rule (no 1N5819 diode yet):** switch the D153C on before plugging in USB, and unplug USB before switching it off.

## Repo layout

```text
firmware/bb1/   the main firmware, grown step by step (firmware/README.md)
firmware/motor_test, imu_test   bring-up sketches
firmware/uf2/   prebuilt files you can drag onto the Pico
tools/          Python helpers (venv: tools/requirements.txt)
cad/            OpenSCAD sources; cad/stl/ the exact STLs printed
docs/           build record, wiring, tuning log, session log, guides
CLAUDE.md       project facts and working rules for Claude Code
```

## Roadmap

- [x] Milestone 1: frame, electronics, bring-up tests
- [x] Pitch angle, two-core firmware, Wi-Fi pages, logging
- [x] Balance loop (PID) tuned on the floor
- [x] Speed / position loop, heading hold, auto-arm, battery voltage
- [ ] Milestone 2: drive from the web page
- [ ] HC-SR04, OLED, 1N5819 diode
- [ ] Pi Zero W + camera

History: [docs/CHANGELOG.md](docs/CHANGELOG.md) and [docs/SESSION_LOG.md](docs/SESSION_LOG.md).

## License

MIT, see [LICENSE](LICENSE).
