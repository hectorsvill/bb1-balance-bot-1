# Self-balancing robot (hectorsvill)

Two-wheel self-balancing robot. Milestone 1 (Pico W + D153C motor driver + MPU-6050) is built and every part has passed its bring-up test.

**Where we are (2026-10-26):** **BB1 balances in place on the wood floor** (TUNING_LOG round 27): P 18 / I 100 / D 0.5, speed loop sp 4 / si 0.5, heading hold th 20, deadband 10, cap 200, trim 0, PWM 10 kHz. Wobble 0.2 deg, no drift, no turning; 30 s arms and a push test passed. Saved in flash and the `config.h` defaults. Key lesson: on the floor the motors act like speed knobs, so the angle loop needs a big I (round 22). Auto-arm (2026-10-24): it starts balancing by itself when stood upright and still for 1 s; reworked the same evening into `auto_arm.h` (named states, lift detector, 3 falls in 30 s = off until ARM), every power-on edge case in `docs/POWER_ON.md`; flashed and working (Hector, 2026-10-25), including switching it on standing up (starts from `GYRO_BIAS_TYPICAL`). Flash with `tools/flash.py`. Low-battery cut-out while armed (2026-10-25, `REASON_BATTERY`): flashed, working — a pack under 6.6 V with the sense wire present disarms and will not re-arm; no wire still skips the check (POWER_ON.md case 15). **Milestone 2 (driving from the web page), branch `drive-from-web`, PR #4, flashed and driving:** `drive.h` moves the speed loop and heading hold targets; `POST /drive` (speed, turn in wheel turns/s, each command lasts 500 ms = the Wi-Fi failsafe), Drive pad on the main page, log format 9. The whole HTTP API is in `docs/API.md` (keep it current with every endpoint change; it ends with what the AI-agent milestone still needs). Left encoder wire fixed 2026-10-25; `enc_fault` detector in `control.h`. Drive tests 2026-10-26 with the new `tools/drive_test.py` (Claude ran them over Wi-Fi; vault `Logs/2026-10-26 bb1 drive tuning.md`) found that **the wheel speed read 3.3x low** since step 5: `constrain(speedL.update(...))` ran `update()` 3 times a tick (`constrain()` is a macro). BB1 drove 2-3x faster than asked, and the lift detector could never trip. Fixed and flashed (log format 10): sp 1.2 (same standing behavior), dff 25, `ENC_FAULT_MIN_SPEED` 500, lift 4 real turns/s; saved in flash. Standing as good or better; forward drive got stuck, so `sdi` (drive speed I, default 2) was added and flashed: forward works (0.46 of 0.5) but an unattended explore run rolled backward with no command (the angle I term holds the wheel speed; the speed loop can't reach it). So a **wheel speed loop** (`smode 1`, `wsp`/`wsi` straight to PWM) and log format 11 were built and flashed 2026-10-26 evening; `/state` confirmed. First smode 1 test (ki 100): standing same as smode 0, but fwd-back-30 drove the wrong way (±0.09 against ±0.3): ki 100 holds pitch so tightly the wheel speed loop's PWM goes straight to the wheels (vault drive-tuning log, 20:31). smode starts at 0 = old behavior. Hector then lowers ki while Claude tests smode 1 and reads the logs. Per-axis holds and the review fixes are flashed. `tools/dataset.py` builds the training table; gaps in FIRMWARE_PLAN.md section 6. ki sweep at smode 1 (100/70/50/35/25, 20:45) also failed: the body never leans into the drive at any ki, BB1 creeps the wrong way, and standing rocks below ki 35. Left at smode 0 / ki 100. Hector chose smode 0 + sdi. Live drive tuning (20:55, vault log): **sp 3** fixes the overshoot after a reverse stop (3 of 3 fwd-back-50 runs: 0.54/−0.50 turns/s, stops under 0.25, was 0.8), standing ±1.0 deg; saved in flash. Unattended explore (21:12, 10 min, 0.9 m box): 0 disarms, never reached the edge (was 41 s); forward gets 0.59 of asked, back 0.77; after stops it still drifts back (11 of 20 stops, up to 0.33 turns/s, angle I −4 to −9) but it fades. sdi sweep (21:30, 3 runs each): **sdi 4** reaches 0.3 forward (was 0.16-0.21) and 0.5 both ways, stops under 0.55; live, not saved yet. **Next:** Hector saves sdi 4; the after-stop creep is still open (firmware I bleed after a drive command, Hector's call), then merge PR #4; then the AI-agent-ready API/CLI milestone. Open: the Pico's server goes quiet 1-2.5 s now and then. Work happens on a branch, then a PR to `main` (CI compiles it). GitHub: https://github.com/hectorsvill/bb1-balance-bot-1 (MIT). The repo is **public** (as of 2026-10-24). Hector is fine with BB1's own-network password being public; what must never be committed is his **home Wi-Fi** name/password (they live only in Pico flash). Engineering docs stay in `docs/` and can be copied to the GitHub wiki with `tools/wiki_sync.sh`. Session, tuning, build-record, and PID-lesson notes live in the second brain (the matching `docs/` files are stubs).

- **Hector does the PID tuning himself**: explain the logs and the theory; give exact values when he asks (he did on 2026-10-24 to get it balancing).
- Every work session is appended to the second brain `Projects/bb1 - balance bot 1/BB1 - Dev Log.md` (newest first). Tuning rounds go in `Logs/` or `BB1 - Tuning Log.md`. `docs/SESSION_LOG.md`, `docs/TUNING_LOG.md`, `docs/BUILD_RECORD.md`, `docs/PID_LESSON.md`, `docs/PID_LEARNING_PROMPT.md`, and `docs/FUTURE_INTEGRATIONS.md` are short stubs so old links keep working: do not paste the narrative back into them. Read logs with `tools/pid_report.py`. Data rules for training a model later: `docs/FIRMWARE_PLAN.md`, section 6.
- BB1 is on Hector's home network at http://bb1.local (its own network "BB1" at http://192.168.42.1 is the fallback; password in the gitignored `secrets.h`). Claude's sandbox blocks the local network: reach the robot with `dangerouslyDisableSandbox` and its IP (`wifi` on serial), since curl to bb1.local can time out.
- Parts not fitted yet: pin and status table in `docs/FUTURE_INTEGRATIONS.md`; the write-ups are in the brain Ideas and Roadmap. Wiring is in `docs/WIRING.md`.

## How to work with me

- I fly FPV (Betaflight): explain PID with quad comparisons. Code names the gains `gainP/gainI/gainD`; serial, JSON and logs still say kp/ki/kd.
- I'm learning embedded and robotics, not just shipping code. PID tuning is the main thing I want to learn: I pick the gains; you explain what the logs show and why, and only suggest numbers if I ask. Explain the why briefly, comment the code so it teaches, and go one small step at a time: I test each step on the hardware and report back before we move on.
- Keep replies concise. Docs in Markdown.
- Steps to follow (tests, setup) are never conversational: a numbered checklist, one action per line, the key or command in bold or code, an **Expect:** line, then what to send back.
- Update the engineering docs with every change (the `docs/` files that are still full, `firmware/README.md`, CHANGELOG, this file's Where we are, vault Dev Log and Dashboard). Session notes and tuning rounds go in the vault, not back into the stubbed files. Use vim (never nano) in any editor instructions.
- I hand-solder everything and don't use header pins.
- Every firmware step keeps logging in mind: new sensor values, commands and outputs go into the telemetry record (see `docs/FIRMWARE_PLAN.md`, section 6). I want to train an AI model on these logs later to drive the bot.
- Code must stay readable enough that I can jump in and change it myself: small files, clear names, comments that explain why. If unsure about a choice, ask me.
- Milestones: 1 = balance + Pico W web page for testing and live PID tuning; 2 = remote control from the web page. More will come.
- Before running anything that can move the motors, confirm with me that the wheels are off the table. Every motor test must stop by itself.

## Firmware structure (bb1)

- Core 1 (`setup1`/`loop1`) owns the IMU, motors and encoders (`motors.h`, encoder interrupts attached on core 1) and runs the 200 Hz loop; it never prints. Motors run only when armed (auto-arm in `auto_arm.h`; `control.h`: arm within 10 deg of trim, auto-disarm past 45 deg, IMU errors, overruns, or 2 s without the page when armed over Wi-Fi) or in a timed test (`m L R ms`, max 3 s / PWM 150). Balance sign: error = pitch - trim, gains positive, positive PWM = forward. Core 0 (`setup`/`loop`) does serial, later Wi-Fi. Core 0 asks core 1 for things through `shared.h` flags; core 1 acts between ticks.
- Core 0 also runs Wi-Fi and the web server (`wifi_setup.h`, `web.h`, `page.h`); the page only reads log records and sets the same `shared.h` requests as serial commands. Home Wi-Fi settings live in Pico flash (EEPROM library); the own-network password is in `secrets.h` (gitignored; template `secrets.example.h`). See `docs/WEB_INTERFACE.md`. Page panels sit in one column (reorderable, wide content scrolls inside its panel); the page lives in flash, not RAM.
- `tools/mock_bb1.py` serves the real `page.h` with fake data at http://localhost:8080 for page work without the robot. Any new endpoint in `web.h` needs a fake answer there too, and a row in `docs/API.md`. Mock logs say `firmware,bb1 MOCK`: never mix them into real data.
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
- **Flash: `tools/venv/bin/python tools/flash.py`** (compile, lying-down check, upload, status; Hector runs it himself when Claude says "flash it"). By hand:
- Compile: `arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir build/<sketch> firmware/<sketch>`
- Upload: `arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn rp2040:rp2040:rpipicow firmware/<sketch>` (find the port with `ls /dev/cu.usbmodem*`). If the Pico doesn't show up, use BOOTSEL and copy the `.uf2` to `/Volumes/RPI-RP2`.
- Serial: 115200 baud, newline line ending. `arduino-cli monitor` is interactive; to read output yourself, use a short pyserial script with a timeout.
- I also use the Arduino IDE 2 with the same board package.

## Repo layout

- `firmware/bb1/` the main firmware (grown step by step), `firmware/motor_test` and `imu_test` bring-up sketches, `firmware/uf2/` prebuilt files
- `tools/` Python helpers with their own venv (`tools/venv`, rebuilt from `tools/requirements.txt`, not committed); run them with `tools/venv/bin/python`
- `cad/` OpenSCAD sources and `cad/stl/` the exact STLs printed
- `docs/` engineering (keep full): WIRING.md, CHANGELOG.md, FIRMWARE_PLAN.md (plan and choices), PID_TUNING.md, FLASHING.md (power order, upload, BOOTSEL, monitor), WEB_INTERFACE.md (connecting, the pages, editing the page), API.md (every HTTP endpoint: arguments, answers, units, drive failsafe), TOOLS.md (every tool, venv, how to run, common errors; keep it current when a tool is added), HANDHELD_TOOL.md (how `tools/handheld.py` works inside), CALIBRATION_AND_LOGS.md (gyro bias cal, readout fields, capturing and summarizing runs; add new log columns here), POWER_ON.md (power-on and auto-arm: every edge case), KIT_REFERENCE.md (what the vendor's motor kit folder says: D153C battery pin, limits). Stubs that point at the second brain: BUILD_RECORD.md, PID_LESSON.md, TUNING_LOG.md, SESSION_LOG.md, PID_LEARNING_PROMPT.md, FUTURE_INTEGRATIONS.md (pin table only)

## Docs

The same docs also live in my Google Drive folder "Self-Balancing Robot" (BOM, Connections sheet, Build Record, Change Log, Wiring Map, Platform Concept). Keep the repo copies in `docs/` current as we go, and tell me when something should be copied to Drive.

## Obsidian vault

Project notes live in `~/Documents/GitHub/hectorsvill_brain/Projects/bb1 - balance bot 1/` (Dashboard, Roadmap, Architecture, Decisions, Dev Log, Ideas, and `Logs/` for raw test-run records). Append sessions to `BB1 - Dev Log.md`. Tuning rounds go in `Logs/` or `BB1 - Tuning Log.md`. The build story is `BB1 - Build Record.md` (or `Logs/`). The PID lesson is in the Learning Log; the learning prompt is a learning asset there. Ideas and Roadmap hold the parts-not-fitted write-ups. Follow the vault's `Projects/CLAUDE.md` rules: append Dev Log entries newest first, add new ADRs instead of editing old ones, keep the Dashboard's Now / Next / Later in sync with the Roadmap. The repo stays the source of truth for pins, signs, wiring, and firmware; vault notes link to it. Obsidian Git auto-commits and pushes the vault, so don't commit there by hand.
