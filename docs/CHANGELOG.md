# Change Log

Newest first.

## Open items

- 1N5819 diode in the D153C 5V to VSYS wire (follow the power rule until then)
- Measure counts per wheel turn cleanly (520 assumed)
- Mount and wire the HC-SR04 on the front end
- Battery cradle (record the pack's size and capacity)
- Next code step: pitch angle (gyro bias calibration + complementary filter), then the balance loop

## 2026-10-21 - Firmware plan drafted

- docs/FIRMWARE_PLAN.md: one `bb1` firmware, control on core 1 at 200 Hz, Wi-Fi page on core 0, UDP logging for analysis and later model training; three choices still open
- docs/PID_TUNING.md: how the PID works, tuning steps for BB1, reading and videos
- Toolchain checked: arduino-cli 1.5.1, rp2040 core 6.3.0, pyserial 3.5; no extra Arduino libraries needed

## 2026-10-20 - Moved to Claude Code

- Project packaged as a repo (balance-bot) with CLAUDE.md holding the pins, signs, power rule and toolchain

## 2026-10-20 - Bring-up results

- Motors: A = left (a- forward), B = right (b+ forward); both encoders count both ways; f / r both "signs correct"
- Direction settings: LEFT_MOTOR_REVERSED = true, RIGHT_MOTOR_REVERSED = false, LEFT_ENC_REVERSED = false, RIGHT_ENC_REVERSED = true
- MPU-6050: live on I2C0, flat face up, gyro y bias about +2.5 deg/s, pitch = tilt X with forward reading negative
- MicroPython replaced by the Arduino sketches

## 2026-10-20 - Bring-up tests written

- motor_test.ino and imu_test.ino (Arduino C++, arduino-pico core 6.3.0)
- Power note corrected: without the diode, switch the D153C on before plugging in USB

## 2026-10-20 - Milestone 1 built

- Short frame v1.1 with the sideways M2 Pico carrier at the 66.6 pin hole, USB toward the back
- D153C on the other face, Dupont sockets on its headers, wires through the windows
- IMU in the base pocket; battery strapped to the printed bottom plate; no diode yet

## 2026-10-20 - Design changes

- Sideways Pico carrier switched from pegs to 4x M2 screw bosses
- v1.2 stack layout designed (everything on one face, 106 tall), not printed; kept as layout = "stack"
- v1.1: column sockets 5.1 to 5.4, bosses 11.5; IMU mount flipped 180; "hectorsvill" engraved; short layout; HC-SR04 holder on the bottom plate

## 2026-10-18 to 2026-10-19

- D153C carrier with 4x M2.5 screws; replacement bottom plate; cyberpunk armor concept parked
- Fit tests passed: board_clear 0.2, peg_clear 0.2, dovetail_clear 0.25, column spacing 88 x 53
