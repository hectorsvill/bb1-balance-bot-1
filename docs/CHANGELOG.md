# Change Log

Newest first.

## Open items

- 1N5819 diode in the D153C 5V to VSYS wire (follow the power rule until then)
- Measure counts per wheel turn cleanly (520 assumed)
- Mount and wire the HC-SR04 on the front end
- Battery cradle (record the pack's size and capacity)
- Hand-held test: fall cut-out (past 45 deg) still to check; measure the motor deadband

## 2026-10-21 - hand-held arm test: directions correct

- First full hand-held run (`tools/handheld.py`): armed 5 s with no Wi-Fi cut-out; leaning forward drove both wheels forward, leaning back drove them backward. Numbers in BUILD_RECORD.md. Fall cut-out not tested yet

## 2026-10-21 - web server: close each connection (fix "lost the web page")

- Hand-held ARM kept disarming after 1 to 2 s with "lost the web page". The 21:04 log showed /state reaching BB1 only every 0.5 to 2.5 s although the page asks 20 times a second
- Cause: the WebServer library serves one connection at a time and, after answering, waits up to 5 s for the browser to close it. One connection left open froze every other request (measured: 4.8 s)
- Fix: web.h `route()` closes the connection after every answer. Re-measured: no more 5 s freeze. A long `/log` answer (Record) can still delay /state by a few hundred ms
- tools/handheld.py: the arm test in one window (a = arm 5 s, Enter = disarm, q = save the log), records over Wi-Fi like logger.py. logger.py's file naming and saving moved into `log_path()` / `save_log()` so both share them
- Decision: for balance tests, arm from serial (`arm 5`, no page heartbeat) and record with `logger.py`, page closed. Steps in `firmware/README.md`. Trim re-measured −3.98 after the battery was refitted, saved in flash

## 2026-10-21 - bb1 step 7: save settings + step test

- settings.h: balance settings saved in flash at EEPROM offset 128 (Wi-Fi uses 0..127), loaded at startup; `save`, `defaults`, page Save to flash / Defaults, `POST /gains/save`, `/gains/defaults`. Disarmed only
- Step test: `step`, page button, `POST /step` (armed only): setpoint +2 deg for 1 s, logged in `setpoint`
- config.h defaults now trim −4.39, cap 80
- Page: Live values List / Tiles switch (List = the original look, default)
- Checked on the robot: kd 0.6 saved, survived a restart; save caused 0 missing ticks, 0 overruns. Current saved: kp 15, ki 0, kd 0.5, trim −4.39, cap 80, deadband 0

## 2026-10-21 - bb1 step 6: arm / disarm + angle loop (not armed on the robot yet)

- Balance trim measured by Hector: −4.39 deg (page, Use current pitch as trim); to become the default in step 7
- First hand-held arm (Hector, 23:19): armed at +8.8 deg with trim 0.16, wheels drove **forward** at the cap (80), about 420 counts/s: direction correct. It disarmed after 1.24 s at +12.5 deg with no fall, IMU or overrun flag, so the cause (button, space bar or the page heartbeat) wasn't recorded; later ARM presses were refused (not upright)
- Added `reason` to every log row (log format 4, in the old padding byte) and a page-hidden disarm (browsers slow hidden tabs to about 1 update a second, which trips the heartbeat)
- Second hand-held run (Hector, 23:31): 3 arms, each stopped after about 1.1 s with reason 4 (lost the page / heartbeat), never a fall. Cause not known yet: the heartbeat compare is now race-proof (signed), `/state` polls time out after 1 s instead of hanging, and every row logs `web_age_ms` (log format 5) to show whether Wi-Fi stalls while the motors run
- Found it: with the motors off and /state polled about 20 times a second, `web_age_ms` reached 1190 ms (0.19 s of 8 s over 1 s): the Wi-Fi chip's power-save naps. `WiFi.noLowPowerMode()` on the home network: max 600 ms, never over 1 s in 10 s, 0 missing ticks
- Arm timer: ARM for N s on the page (default 5), `arm N` on serial, `seconds=` on /arm; reason 11
- Page: Record and Live log combined into one Log panel (recording shows the records as they arrive); gain boxes turn orange when they differ from BB1 and Apply says Apply changes; Enter applies; hover / tap hints on every non-obvious control
- Page redesign (Hector asked): status bar always on top with mode, arm countdown and STOP; Balance panel with a big countdown and progress bar, gain fields with units; new view-only PID panel (tiles + 10 s chart of P, I, D, output, ±cap); Log as a table with a sticky header, tinted armed rows, short / all columns; tiles instead of tables; panels fold; hints on hover. `/state` gained `arm_left_ms`. Page is now about 47 KB of flash
- docs: Balance panel control by control (WEB_INTERFACE.md), what the hand-held test is for (PID_TUNING.md)

- pid.h (derivative on the gyro rate, clamped integral, no windup while saturated), control.h (disarmed / armed / fallen, arm only within 10 deg of trim and calibrated; disarm on > 45 deg, 5 IMU errors or overruns in a row, or 1 s without the page while armed from it)
- Live gains: serial `arm`, `disarm`, `kp/ki/kd N`, `trim N` / `trim here`, `cap N`, `deadband N`, `gains`; page Balance panel; `POST /arm`, `/disarm`, `/gains`. STOP / space bar / Enter also disarm. Defaults in config.h (Kp 15, Ki 0, Kd 0.5, cap 150), not saved yet
- Sign convention: error = pitch - trim, all gains positive (docs/PID_TUNING.md updated)
- Log format 3: setpoint, p_term, i_term, d_term (stored x10 as int16), gains_ver; mode 4 armed, 5 fallen; flag 8 saturated; `# gains_ver,...` meta line. Record 68 bytes, buffer 136 KB; heap 40 KB free on the robot
- Mock arms, refuses when not upright, and shows the PD numbers
- Uploaded. Self-check disarmed, logger 15 s with /state polled at 20 Hz: 2947 records, 0 lost, 0 missing ticks, spacing 4997 to 5003 us, work 516 avg / 743 max

## 2026-10-21 - bb1 step 5: motors + encoders

- motors.h: TB6612 driver (forward +, STBY low unless a test runs), encoder interrupts on core 1, wheel speed over 10 ticks
- Timed motor test from serial (`m L R ms`, `stop`, Enter), the page (Motor test panel, STOP / space bar) and `POST /motor`, `/motor/stop`; capped at PWM 150 and 3 s; mode 3 while running
- LogRecord 40 -> 56 bytes (enc_l, enc_r, speed_l, speed_r, pwm_l, pwm_r); log format 2; `# counts_per_rev...` meta line; buffer 112 KB
- check_dump.py: wheel summary and a speed / PWM plot; mock_bb1.py fakes the motors (17.5 counts/s per PWM)
- Compiles: 20% flash, 70% RAM static, heap 64 KB free on the robot
- Motor test passed on the robot (Hector, 2026-10-21). Uploaded (the Pico came back after Hector replugged it). Self-check with no motors, logger.py 21 s over Wi-Fi while /state was polled at 20 Hz: 4254 records, 0 lost, 0 missing ticks, spacing 4997 to 5002 us, work 500 us avg / 716 max, 0 errors. That also covers step 4's logging path.

## 2026-10-21 - bb1 step 4: saving logs, live log view

- web.h: `/log?since=N` (CSV, up to 1000 records per request, sent in chunks, reports lost records), `/log/meta`, `/sys` (RAM and flash use)
- log_meta.h: one block of `# setting` lines (firmware, log_format, bias, alpha, scales, signs) shared by the serial dump, the page and logger.py
- page.h: Record / Stop / Download, Save last 10 s, Live log panel (every record, 10th or 40th; scrolls sideways), Pico memory panel; panels in one column, reorderable with arrows (saved per browser)
- Startup calibration retries every 3 s if the robot was moving; new flag bit 4 (no bias yet); `ever_cal` in /state
- tools/logger.py (record over Wi-Fi), tools/mock_bb1.py (offline mock serving the real page); check_dump.py reads all three file sources and keeps text settings
- docs: WEB_INTERFACE.md (saving logs, endpoints, offline mock), TOOLS.md, CALIBRATION_AND_LOGS.md, FLASHING.md (battery care, upload troubleshooting), new FUTURE_INTEGRATIONS.md (battery divider, OLED, Pi Zero W + camera, HC-SR04)
- Compiles: 20% flash (421 KB), 58% RAM static, 109 KB free. Tested against the mock: logger 782 records 0 missing; page Record 415 records 0 gaps; layout fits a 375 px phone with the log scrolling inside its panel.

## 2026-10-21 - bb1 step 3: Wi-Fi + web page

- wifi_setup.h: own network "BB1" (192.168.42.1) by default; home network saved to flash (EEPROM library) and joined at startup, with mDNS bb1.local; 15 s fallback to the own network; `wifi`, `wifi reset`
- web.h + page.h: page with live pitch / accel pitch chart (10 s), live values, Calibrate, Set alpha, Network panel; endpoints /state, /cal, /alpha, /wifi, /wifi/forget
- secrets.h (gitignored) + secrets.example.h; `quiet` serial command; docs/WEB_INTERFACE.md
- Compiles: 19% flash, 109 KB RAM free
- Tested: Hector joined the page on the own network, then saved his home network; page works at bb1.local. With /state polled at 20 Hz: 0 missing ticks, spacing 4997 to 5003 us (was exactly 5000: core 0's Wi-Fi code now shares the flash cache), work 494 us avg / 659 max, request latency 41 ms median. Step 3 passed.

## 2026-10-21 - bb1 step 2: two cores + log buffer

- Control loop moved to core 1 (setup1/loop1, own 8 KB stack); serial on core 0; requests via shared.h
- telemetry.h: 40-byte LogRecord per tick (tick, time, work, mode, flag_bits, raw IMU, pitch, rate, accel pitch), 10 s ring buffer (80 KB) guarded by a mutex; `dump` / `dump N` print it as CSV with settings lines
- tools/check_dump.py; log columns documented in docs/CALIBRATION_AND_LOGS.md
- Self-check on the robot: 1800 records, 0 missing ticks, spacing exactly 5000 us, work 451 us avg / 484 max, 0 errors
- Hector's test (tilting, pitch -15.9 to +88.3): 1800 records, 0 missing ticks, exactly 5000 us, 451 / 474 us, 0 errors. Step 2 passed.

## 2026-10-21 - bb1 step 1: pitch angle

- Tested on the robot: filter tracks the accel angle within ~0.3 deg, loop 446 us avg / 527 us max of 5000, 0 overruns. Forward lean read negative, so ACCEL_PITCH_SIGN and GYRO_PITCH_SIGN changed to +1 (the bring-up note "forward reads negative" was wrong). Upright reads about +6 deg.
- Rerun after the sign fix and `cal`: lying on its front +88.7 deg, rock steady for 20 s with rate 0.0 (bias x -0.50, y +2.07, z -2.20 deg/s); forward tilt positive with positive rate; upright about -2.5 deg; filter within 0.3 deg of the accel angle at rest. Step 1 passed.

- firmware/bb1: config.h, imu.h (gyro +/-500 deg/s, 44 Hz DLPF, 200 Hz, bias calibration), angle.h (complementary filter, alpha 0.98), 200 Hz fixed loop with loop-time stats; motors held off
- Plan updated with Hector's choices: own network by default plus a saved home-network setting (falls back to own network); web page before PID; logs saved from the browser or tools/logger.py, same CSV
- tools/: Python venv (tools/venv, not committed), requirements.txt, serial_log.py

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
