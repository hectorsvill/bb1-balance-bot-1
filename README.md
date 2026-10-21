# BB1 - Balance Bot 1

A two-wheel self-balancing robot, built by hand and programmed step by step as a way to learn embedded systems and control.

**Status (2026-10-21):** Milestone 1 is built and every part has passed its bring-up test (motors, encoders, IMU). Next up is a steady pitch angle, then the PID balance loop, then driving.

## Hardware

| Part | What it does |
|---|---|
| Raspberry Pi Pico W (RP2040) | Main controller |
| D153C dual motor driver (TB6612) | Drives both motors from the battery |
| 2x MG310P20 7.4 V gear motors (20:1) with Hall encoders | Wheels, plus wheel speed and distance |
| GY-521 (MPU-6050) | Accelerometer and gyro for the tilt angle |
| WHEELTEC 2-wheel chassis, 2S LiPo | Base and power |
| 3D-printed reverse-T frame and carriers | Holds the boards; sources in `cad/` |

Full pin map: [docs/WIRING.md](docs/WIRING.md). What was actually built and tested: [docs/BUILD_RECORD.md](docs/BUILD_RECORD.md).

> **Power rule (no 1N5819 diode yet):** switch the D153C on before plugging in USB, and unplug USB before switching it off.

## Repo layout

```
cad/            OpenSCAD sources
cad/stl/        the exact STLs that were printed
firmware/       Arduino sketches, one folder each (see firmware/README.md)
firmware/uf2/   prebuilt files you can drag onto the Pico
docs/           BUILD_RECORD, WIRING, CHANGELOG
CLAUDE.md       project facts and working rules for Claude Code
```

## Build and flash

Arduino C++ on the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core (tested with 6.3.0), board "Raspberry Pi Pico W".

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/motor_test firmware/motor_test
```

```bash
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn rp2040:rp2040:rpipicow firmware/motor_test
```

Find the port with `ls /dev/cu.usbmodem*`. Serial is 115200 baud. Setup steps and the bring-up tests are in [firmware/README.md](firmware/README.md).

## Roadmap

- [x] Milestone 1: frame printed, electronics wired, motors / encoders / IMU tested
- [ ] Pitch angle: gyro bias calibration + complementary filter
- [ ] Balance loop (PID)
- [ ] Driving
- [ ] HC-SR04, OLED, battery sense, 1N5819 diode
- [ ] Pi Zero W (next milestone)

History: [docs/CHANGELOG.md](docs/CHANGELOG.md).

Project notes (roadmap, decisions, dev log, test logs) live in my Obsidian vault under `Projects/bb1 - balance bot 1/`.
