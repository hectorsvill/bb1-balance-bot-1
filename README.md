# BB1 - Balance Bot 1

A two-wheel self-balancing robot, hand-soldered and programmed one small step at a time to learn embedded systems and PID control.

<!-- MEDIA TODO: hero photo of BB1 standing on the floor (planned path: docs/media/bb1.jpg) -->

**Status (2026-10-31):** BB1 balances, drives from its web page or an AI agent over Wi-Fi, and flashes itself over Wi-Fi. Milestone 4 (fast driving and spins) is under way: it reaches 1.5 wheel turns/s (0.27 m/s) on wood with room to spare (top speed set to 3.0, 0.53 m/s, being tested in steps), and spins a full 360 on the spot in about 1 s without falling. Next: exact-angle spins, a drive mode that holds the lean while you hold the stick, and the faster speed steps on a longer floor.

<!-- MEDIA TODO: short GIF or video of a fast 360 spin and a forward-back drive (planned path: docs/media/spin.gif) -->

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

<!-- MEDIA TODO: close-up photos of the electronics, Pico side and D153C side (planned path: docs/media/electronics.jpg) -->

## How it balances

```text
IMU -> complementary filter -> pitch (gyro corrected for turning)
pitch -> angle PID (P 18, I 100, D 0.5) -> PWM to both wheels
wheel speed + distance -> speed loop -> moves the angle target (stop drifting, hold position)
left vs right distance -> heading hold -> steers the wheels apart (stay straight, or turn)
drive command -> ramps the speed and heading targets; leans into acceleration (feedforward)
each wheel: balance +- steering + its own deadband -> motor
```

The surprise: on the floor these geared motors act like a *speed* knob, not a *push* knob, so the angle loop needs a big I term (P and D alone always ran away). Later lessons: a balancing robot can only hold a lean while it speeds up; the motor deadband has to go on each wheel or turns judder; and an IMU that sits 2.5 deg off level reads part of every spin as tilt. The whole tuning story, round by round, is in [docs/TUNING_LOG.md](docs/TUNING_LOG.md); the theory, with FPV / Betaflight comparisons, in [docs/PID_LESSON.md](docs/PID_LESSON.md).

## Web pages (on the Pico)

- `http://bb1.local/tune`: change every gain live, ARM with a timer, STOP, live P / I / D terms, battery per cell.
- `http://bb1.local`: status, ARM / STOP and the **Drive** pad (milestone 2) with a max-speed slider; folded below, everything else (motor test, logs, sensors, Wi-Fi setup).

<!-- MEDIA TODO: screenshots of the main page with the Drive pad, and of /tune (planned path: docs/media/page.png, docs/media/tune.png) -->

Details: [docs/WEB_INTERFACE.md](docs/WEB_INTERFACE.md). Everything the pages do is a plain HTTP API, usable from curl or a script: [docs/API.md](docs/API.md). The pages have **no login**: anyone on the same Wi-Fi can arm the robot. Use it on your own network.

## AI agent control (milestone 3)

An AI agent (Claude Code, for example) can drive BB1 with `tools/bb1.py` (a command line tool with JSON answers and its own safety checks) or as MCP tools (`tools/bb1_mcp.py`). The manual it reads first is [docs/AGENT.md](docs/AGENT.md); the prompt to start a session is [docs/AGENT_PROMPT.md](docs/AGENT_PROMPT.md). `GET /api` describes every endpoint and limit, and new firmware goes on over Wi-Fi (`tools/flash.py --ota`).

## Logging (for tuning now, for training a model later)

Every 5 ms tick is logged: raw IMU, pitch, wheel encoders and speeds, the P / I / D terms, PWM to each motor, the angle target, the drive command and targets, battery voltage, why it disarmed. `tools/logger.py`, `tools/drive_test.py` and the agent's automatic recorder pull the log over Wi-Fi into a CSV, and every log lands in one SQLite catalog (`tools/runs_db.py`, read with `tools/logs.py`); `tools/pid_report.py` summarizes each try. Columns: [docs/CALIBRATION_AND_LOGS.md](docs/CALIBRATION_AND_LOGS.md). Data rules: [docs/FIRMWARE_PLAN.md](docs/FIRMWARE_PLAN.md), section 6. Recorded logs stay out of this repo (`logs/` is gitignored).

<!-- MEDIA TODO: a plot of one drive test (pitch, wheel speed, PWM) from tools/check_dump.py --plot (planned path: docs/media/drive_plot.png) -->

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

Or drag `firmware/uf2/bb1.ino.uf2` onto the Pico in BOOTSEL mode (its own-network password is the template's `change-me-please`). After the first USB flash, `tools/venv/bin/python tools/flash.py --ota --execute` updates it over Wi-Fi (BB1 must be lying down). Without `--execute`, `flash.py` only prints a JSON plan. Full guide, Arduino IDE steps and troubleshooting: [docs/FLASHING.md](docs/FLASHING.md). Python tools: [docs/TOOLS.md](docs/TOOLS.md).

> **Safety.** With auto-arm on, BB1 starts driving its wheels 1 s after it's held upright and still: lay it down to carry it (a lift detector stops the wheels if they spin up in the air; see [docs/POWER_ON.md](docs/POWER_ON.md)). Every motor run stops by itself (45 deg fall cut-out, arm timer, STOP). **Power:** a 1N5819 diode in the 5V to VSYS wire (2026-10-28) makes USB and the battery safe in any order.

## Repo layout

```text
firmware/bb1/   the main firmware, grown step by step (firmware/README.md)
firmware/motor_test, imu_test   bring-up sketches
firmware/uf2/   prebuilt files you can drag onto the Pico
tools/          Python helpers (venv: tools/requirements.txt)
cad/            OpenSCAD sources; cad/stl/ the exact STLs printed
docs/           wiring, API, agent manual, flashing, tuning, logs, plan, changelog (session and tuning notes live in a private notebook; those files are stubs)
CLAUDE.md       project facts and working rules for Claude Code
```

## Roadmap

- [x] Milestone 1: frame, electronics, bring-up tests
- [x] Pitch angle, two-core firmware, Wi-Fi pages, logging
- [x] Balance loop (PID) tuned on the floor
- [x] Speed / position loop, heading hold, auto-arm, battery voltage
- [x] Milestone 2: drive from the web page (2026-10-26)
- [x] Milestone 3: AI agent control: `bb1.py`, `GET /api`, MCP server, agent manual, over-the-air updates (2026-10-28)
- [ ] Milestone 4: fast driving and spins (in progress: 1.5 turns/s driving and 1 s 360s done; faster speeds, exact-angle spins next)
- [x] 1N5819 diode in the 5V to VSYS wire (2026-10-28)
- [ ] HC-SR04, OLED
- [ ] Pi Zero W + camera

History: [docs/CHANGELOG.md](docs/CHANGELOG.md).

## License

MIT, see [LICENSE](LICENSE).
