# Self-balancing robot (hectorsvill)

Two-wheel self-balancing robot. Milestone 1 (Pico W + D153C motor driver + MPU-6050) is built and every part has passed its bring-up test.

**Breakthrough 2026-10-24:** P 18 / **I 100** / D 0.5 / speed loop sp 4 balances on wood: 0.2 deg wobble, no jitter, 10 s every try (saved in flash). The motors act as speed knobs on the floor, so the angle loop needs a big I (TUNING_LOG round 22). Drift down to 2 to 4 wheel turns per 10 s; next si (hold position) and heading hold `th` (stops the left curve; written, not flashed). **Earlier (2026-10-23):** tuning on the **wood floor**, no USB cable (arm from /tune or `handheld.py --wifi`). Balance point moved to about **+0.55 deg** after the STBY repair; motor deadband about 15 to 30 on wood (`tools/deadband_test.py`); P 9.85 / D 0.74 only just holds it. **Next:** step 8 speed loop flashed 2026-10-24 (`sp`, `si` on /tune), now tune sp / si on wood with P 18 / D 0.7 / deadband 10 / cap 200, then the plan in `docs/TUNING_LOG.md` ("Plan": deadband, trim, P, D, step test, then step 8 speed loop). **Hector does the PID tuning himself**: explain the logs and the theory, don't choose gains unless he asks. (2026-10-24 he asked for exact values to test, to get it balancing: give them, with the why.) Every work session goes in `docs/SESSION_LOG.md`, every tuning round in `docs/TUNING_LOG.md`; read logs with `tools/pid_report.py`; theory in `docs/PID_LESSON.md`. Data rules for training a model later: `docs/FIRMWARE_PLAN.md`, section 6. BB1 is on Hector's home network at http://bb1.local (own network "BB1" / `bb1robot` at http://192.168.42.1 is the fallback). Claude's sandbox blocks the local network: reach the robot with `dangerouslyDisableSandbox` and its IP (192.168.1.240 lately; `wifi` on serial), since curl to bb1.local can time out. Parts not fitted yet: `docs/FUTURE_INTEGRATIONS.md`.

## How to work with me

- I fly FPV (Betaflight): explain PID with quad comparisons. Code names the gains `gainP/gainI/gainD`; serial, JSON and logs still say kp/ki/kd.
- I'm learning embedded and robotics, not just shipping code. PID tuning is the main thing I want to learn: I pick the gains; you explain what the logs show and why, and only suggest numbers if I ask. Explain the why briefly, comment the code so it teaches, and go one small step at a time: I test each step on the hardware and report back before we move on.
- Keep replies concise. Docs in Markdown.
- Steps to follow (tests, setup) are never conversational: a numbered checklist, one action per line, the key or command in bold or code, an **Expect:** line, then what to send back.
- Update the docs with every change (repo `docs/`, `firmware/README.md`, CHANGELOG, SESSION_LOG, this file's Where we are, vault Dev Log and Dashboard). Use vim (never nano) in any editor instructions.
- I hand-solder everything and don't use header pins.
- Every firmware step keeps logging in mind: new sensor values, commands and outputs go into the telemetry record (see `docs/FIRMWARE_PLAN.md`, section 6). I want to train an AI model on these logs later to drive the bot.
- Code must stay readable enough that I can jump in and change it myself: small files, clear names, comments that explain why. If unsure about a choice, ask me.
- Milestones: 1 = balance + Pico W web page for testing and live PID tuning; 2 = remote control from the web page. More will come.
- Before running anything that can move the motors, confirm with me that the wheels are off the table. Every motor test must stop by itself.

## Firmware structure (bb1)

- Core 1 (`setup1`/`loop1`) owns the IMU, motors and encoders (`motors.h`, encoder interrupts attached on core 1) and runs the 200 Hz loop; it never prints. Motors run only when armed (`control.h`: arm within 10 deg of trim, auto-disarm past 45 deg, IMU errors, overruns, or 2 s without the page when armed over Wi-Fi) or in a timed test (`m L R ms`, max 3 s / PWM 150). Balance sign: error = pitch - trim, gains positive, positive PWM = forward. Core 0 (`setup`/`loop`) does serial, later Wi-Fi. Core 0 asks core 1 for things through `shared.h` flags; core 1 acts between ticks.
- Core 0 also runs Wi-Fi and the web server (`wifi_setup.h`, `web.h`, `page.h`); the page only reads log records and sets the same `shared.h` requests as serial commands. Home Wi-Fi settings live in Pico flash (EEPROM library); the own-network password is in `secrets.h` (gitignored; template `secrets.example.h`). See `docs/WEB_INTERFACE.md`. Page panels sit in one column (reorderable, wide content scrolls inside its panel); the page lives in flash, not RAM.
- `tools/mock_bb1.py` serves the real `page.h` with fake data at http://localhost:8080 for page work without the robot. Any new endpoint in `web.h` needs a fake answer there too. Mock logs say `firmware,bb1 MOCK`: never mix them into real data.
- Every log file (serial `dump`, page Download, `logger.py`) is the same layout: `# setting` lines from `log_meta.h`, then the CSV. Bump `LOG_FORMAT` when columns change.
- Every tick fills one `LogRecord` (`telemetry.h`) into a mutex-guarded ring buffer. New fields go at the end of the struct, in `csvHeader()`/`printCsv()`, and in the column table in `docs/CALIBRATION_AND_LOGS.md`.

## Directions and signs (measured 2026-10-20)

- **Front** = the "hectorsvill" side. **Back** = the side with the Pico's USB and the D153C motor plugs. Left and right as seen from behind, facing the front. **"Laying on its front"** = lying down with the hectorsvill side facing the floor (reads about +89 deg pitch); "on its back" = the USB side down. (The .scad files call the "hectorsvill" flange the rear; their +Y is the robot's back.)
- Motor A (D153C socket A) = left wheel; motor B (socket B) = right wheel.
- Code settings that make forward positive (confirmed with the f / r test, "signs correct"):
  - `LEFT_MOTOR_REVERSED = true` (a- rolls forward)
  - `RIGHT_MOTOR_REVERSED = false` (b+ rolls forward)
  - `LEFT_ENC_REVERSED = false` (encoder 1 counts up rolling forward)
  - `RIGHT_ENC_REVERSED = true` (encoder 2 counts down rolling forward)
- Encoders: interrupt on both edges of channel A, direction from channel B. 520 counts per wheel turn assumed (13 pulses x 20:1 x 2 edges), not measured cleanly yet. About 1,750 counts/s per wheel at PWM 100/255 with the wheels in the air.
- MPU-6050: mounted flat, face up (accel z about +0.94 g at rest). Pitch is the tilt about the chip's X axis, `atan2(ay, az)`, and gyro X is the pitch rate. **Both read positive when leaning forward, no negation** (measured 2026-10-21 in bb1 step 1: hectorsvill side down read about +26 deg, gyro rate shares the sign; the earlier "-35 deg forward" bring-up note was wrong). `ACCEL_PITCH_SIGN` / `GYRO_PITCH_SIGN` = +1 in `firmware/bb1/config.h`. Balance trim measured −4.39 deg (2026-10-21, step 6; the earlier +6 deg hand-held reading was before the bias/sign fixes). Gyro bias at rest about +2.5 deg/s on Y; measured on all three axes at startup.

## Hardware

- WHEELTEC 2-wheel chassis, 2x MG310P20 7.4 V Hall-encoder gear motors (20:1), 4x 40 mm brass hex columns, printed bottom plate
- Raspberry Pi Pico W (RP2040) on a sideways carrier, 4x M2 screws
- D153C dual motor driver (TB6612) on the other face of the spine, 4x M2.5 screws; Dupont sockets on its header pins
- GY-521 (MPU-6050) in the base pocket on the Pico side
- 2S 1300 mAh LiPo strapped to the bottom plate (no voltage sensing yet: check it by hand, never below 3.0 V per cell; see `docs/FLASHING.md`, Battery care)
- 3D-printed reverse-T top frame (short layout, 84 tall, wire windows); sources in `cad/`
- Not yet fitted: HC-SR04 (holder goes on the front end), SSD1306 OLED, Pi Zero W (next milestone), 1N5819 diode. Battery voltage: D153C `ADC` pin (VIN/11) wired to GP26 2026-10-24; firmware `battery.h` reads it on core 0, logs `batt_v` (whole pack, log format 6), pages show **volts per cell** (Hector prefers per cell); calibrated factor 12.07 (`BATTERY_DIVIDER`)

## Pico W pins

| Function | GPIO | Pico pin |
|---|---|---|
| IMU SDA (I2C0) | GP4 | 6 |
| IMU SCL (I2C0) | GP5 | 7 |
| IMU GND | GND | 8 |
| IMU VCC | 3V3(OUT) | 36 |
| D153C STBY (HIGH = on) | GP9 | 12 |
| D153C GND | GND | 13 |
| PWMA (left) | GP10 | 14 |
| PWMB (right) | GP11 | 15 |
| AIN1 / AIN2 | GP12 / GP13 | 16 / 17 |
| BIN1 / BIN2 | GP14 / GP15 | 19 / 20 |
| E1A / E1B (left encoder) | GP16 / GP17 | 21 / 22 |
| E2A / E2B (right encoder) | GP18 / GP19 | 24 / 25 |
| D153C GND (power) | GND | 38 |
| D153C 5V | VSYS | 39 |
| Reserved: HC-SR04 Trig / Echo | GP20 / GP21 | 26 / 27 |
| Reserved: OLED I2C1 | GP6 / GP7 | 9 / 10 |
| Reserved: Pi Zero UART | GP0 / GP1 | 1 / 2 |
| Reserved: battery sense | GP26 | 31 |

PWM at 10 kHz since 2026-10-24 (was 20 kHz; lower deadband, BUILD_RECORD), `analogWriteRange(255)`. PWMA and PWMB share one PWM slice.

## Power rule (no 1N5819 diode yet)

D153C 5V goes straight to VSYS. Switch the D153C on (battery connected) **before** plugging in USB, and unplug USB **before** switching it off. USB alone back-feeds the D153C's 5V regulator. The fix is a 1N5819 in that wire, stripe toward the Pico.

## Toolchain

- Arduino C++ on the arduino-pico core by Earle Philhower (tested with 6.3.0). Board "Raspberry Pi Pico W", FQBN `rp2040:rp2040:rpipicow`.
- Board manager URL: `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
- Compile: `arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/<sketch> firmware/<sketch>`
- Upload: `arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn rp2040:rp2040:rpipicow firmware/<sketch>` (find the port with `ls /dev/cu.usbmodem*`). If the Pico doesn't show up, use BOOTSEL and copy the `.uf2` to `/Volumes/RPI-RP2`.
- Serial: 115200 baud, newline line ending. `arduino-cli monitor` is interactive; to read output yourself, use a short pyserial script with a timeout.
- I also use the Arduino IDE 2 with the same board package.

## Repo layout

- `firmware/bb1/` the main firmware (grown step by step), `firmware/motor_test` and `imu_test` bring-up sketches, `firmware/uf2/` prebuilt files
- `tools/` Python helpers with their own venv (`tools/venv`, rebuilt from `tools/requirements.txt`, not committed); run them with `tools/venv/bin/python`
- `cad/` OpenSCAD sources and `cad/stl/` the exact STLs printed
- `docs/` BUILD_RECORD.md (as built + test results), WIRING.md, CHANGELOG.md, FIRMWARE_PLAN.md (plan and choices), PID_TUNING.md, PID_LESSON.md, TUNING_LOG.md (every tuning round and the plan), SESSION_LOG.md (one entry per work session), FLASHING.md (power order, upload, BOOTSEL, monitor), WEB_INTERFACE.md (connecting, endpoints, editing the page), TOOLS.md (every tool, venv, how to run, common errors; keep it current when a tool is added), HANDHELD_TOOL.md (how `tools/handheld.py` works inside), CALIBRATION_AND_LOGS.md (gyro bias cal, readout fields, capturing and summarizing runs; add new log columns here), FUTURE_INTEGRATIONS.md (parts not fitted yet: wiring, power, which to buy), KIT_REFERENCE.md (what the vendor's motor kit folder says: D153C battery pin, limits)

## Docs

The same docs also live in my Google Drive folder "Self-Balancing Robot" (BOM, Connections sheet, Build Record, Change Log, Wiring Map, Platform Concept). Keep the repo copies in `docs/` current as we go, and tell me when something should be copied to Drive.

## Obsidian vault

Project notes live in `/Users/hectorstevenvillasano/Documents/GitHub/hectorsvill_brain/Projects/bb1 - balance bot 1/` (Dashboard, Roadmap, Architecture, Decisions, Dev Log, Ideas, and `Logs/` for raw test-run records). Follow the vault's `Projects/CLAUDE.md` rules: append Dev Log entries newest first, add new ADRs instead of editing old ones, keep the Dashboard's Now / Next / Later in sync with the Roadmap. The repo stays the source of truth for pins, signs and test numbers; vault notes link to it. Obsidian Git auto-commits and pushes the vault, so don't commit there by hand.
