# Self-balancing robot (hectorsvill)

Two-wheel self-balancing robot. Milestone 1 (Pico W + D153C motor driver + MPU-6050) is built and every part has passed its bring-up test.

**Next step:** a steady pitch angle (gyro bias calibration at startup + complementary filter), printed live so I can tilt the robot and watch it. After that: the balance loop (PID), then driving. The full plan (two cores, Wi-Fi tuning page, logging for later model training) is in `docs/FIRMWARE_PLAN.md`; tuning notes in `docs/PID_TUNING.md`.

## How to work with me

- I'm learning embedded and robotics, not just shipping code. Explain the why briefly, comment the code so it teaches, and go one small step at a time: I test each step on the hardware and report back before we move on.
- Keep replies concise. Docs in Markdown. Use vim (never nano) in any editor instructions.
- I hand-solder everything and don't use header pins.
- Before running anything that can move the motors, confirm with me that the wheels are off the table. Every motor test must stop by itself.

## Directions and signs (measured 2026-10-20)

- **Front** = the "hectorsvill" side. **Back** = the side with the Pico's USB and the D153C motor plugs. Left and right as seen from behind, facing the front. (The .scad files call the "hectorsvill" flange the rear; their +Y is the robot's back.)
- Motor A (D153C socket A) = left wheel; motor B (socket B) = right wheel.
- Code settings that make forward positive (confirmed with the f / r test, "signs correct"):
  - `LEFT_MOTOR_REVERSED = true` (a- rolls forward)
  - `RIGHT_MOTOR_REVERSED = false` (b+ rolls forward)
  - `LEFT_ENC_REVERSED = false` (encoder 1 counts up rolling forward)
  - `RIGHT_ENC_REVERSED = true` (encoder 2 counts down rolling forward)
- Encoders: interrupt on both edges of channel A, direction from channel B. 520 counts per wheel turn assumed (13 pulses x 20:1 x 2 edges), not measured cleanly yet. About 1,750 counts/s per wheel at PWM 100/255 with the wheels in the air.
- MPU-6050: mounted flat, face up (accel z about +0.94 g at rest). Pitch is the tilt about the chip's X axis, `atan2(ay, az)`. Tipping forward about 30 deg read -35 deg, so negate it to make forward lean positive. Gyro X is the pitch rate and should share that sign (verify in the angle step). Gyro bias at rest about +2.5 deg/s on Y; measure all three axes at startup.

## Hardware

- WHEELTEC 2-wheel chassis, 2x MG310P20 7.4 V Hall-encoder gear motors (20:1), 4x 40 mm brass hex columns, printed bottom plate
- Raspberry Pi Pico W (RP2040) on a sideways carrier, 4x M2 screws
- D153C dual motor driver (TB6612) on the other face of the spine, 4x M2.5 screws; Dupont sockets on its header pins
- GY-521 (MPU-6050) in the base pocket on the Pico side
- 2S LiPo strapped to the bottom plate
- 3D-printed reverse-T top frame (short layout, 84 tall, wire windows); sources in `cad/`
- Not yet fitted: HC-SR04 (holder goes on the front end), SSD1306 OLED, Pi Zero W (next milestone), 1N5819 diode, battery voltage divider

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

PWM at 20 kHz, `analogWriteRange(255)`. PWMA and PWMB share one PWM slice.

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

- `firmware/` sketches (one folder per sketch), `firmware/uf2/` prebuilt files
- `cad/` OpenSCAD sources and `cad/stl/` the exact STLs printed
- `docs/` BUILD_RECORD.md (as built + test results), WIRING.md, CHANGELOG.md, FIRMWARE_PLAN.md (draft plan, open choices), PID_TUNING.md

## Docs

The same docs also live in my Google Drive folder "Self-Balancing Robot" (BOM, Connections sheet, Build Record, Change Log, Wiring Map, Platform Concept). Keep the repo copies in `docs/` current as we go, and tell me when something should be copied to Drive.

## Obsidian vault

Project notes live in `/Users/hectorstevenvillasano/Documents/GitHub/hectorsvill_brain/Projects/bb1 - balance bot 1/` (Dashboard, Roadmap, Architecture, Decisions, Dev Log, Ideas, and `Logs/` for raw test-run records). Follow the vault's `Projects/CLAUDE.md` rules: append Dev Log entries newest first, add new ADRs instead of editing old ones, keep the Dashboard's Now / Next / Later in sync with the Roadmap. The repo stays the source of truth for pins, signs and test numbers; vault notes link to it. Obsidian Git auto-commits and pushes the vault, so don't commit there by hand.
