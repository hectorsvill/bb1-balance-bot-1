# Change Log

Newest first.

## Open items

- 1N5819 diode in the D153C 5V to VSYS wire (follow the power rule until then)
- Measure counts per wheel turn cleanly (520 assumed)
- Mount and wire the HC-SR04 on the front end
- Battery cradle (record the pack's size and capacity)

## 2026-10-25 - low battery disarms while armed

- **Safety:** a pack under `BATTERY_LOW_V` (6.6 V, 3.3 V per cell) only blocked auto-arm. Once balancing, the motors kept driving as the voltage sagged. `balanceTick()` now disarms with `REASON_BATTERY` (13): motors braked, STBY low. `balanceArm()` refuses with the same reason. Both use `batteryTooLow()` in `battery.h` (moved out of `auto_arm.h`)
- No sense wire (under `BATTERY_MIN_SENSE_V`): still no battery check
- The disarm is not a fall, so auto-arm stays off until ARM (the voltage recovers as soon as the motors stop). `/state` `reason` is "battery low (under 3.3 V per cell)". `pid_report.py` names reason 13 "battery" (and 12 "picked up")
- Docs: POWER_ON.md case 15. Not flashed yet

## 2026-10-25 - narrative docs moved; compile CI

- Session, tuning, build-record, and PID-lesson notes now live in the second brain (`Projects/bb1 - balance bot 1/`). These repo files are short stubs so old links keep working: `SESSION_LOG.md` → `BB1 - Dev Log.md`; `TUNING_LOG.md` → `Logs/` or `BB1 - Tuning Log.md`; `BUILD_RECORD.md` → `BB1 - Build Record.md` or `Logs/`; `PID_LESSON.md` → Learning Log; `PID_LEARNING_PROMPT.md` → learning asset; `FUTURE_INTEGRATIONS.md` → Ideas and Roadmap (pin table kept here)
- Deleted the orphan serial dump `tools/step1.txt` (README and TOOLS.md did not link to it)
- GitHub Actions (`.github/workflows/compile.yml`) compiles `firmware/bb1` for Raspberry Pi Pico W (`rp2040:rp2040:rpipicow`, Earle Philhower core 6.3.0) on push and pull request to main

## 2026-10-25 - power-on work finished

- Flashed from branch `power-on` with `tools/flash.py`: the low-battery cut-out plus everything from 2026-10-24. Hector: power-on behavior working as intended

## 2026-10-24 - tools/flash.py

- `tools/flash.py`: compile, check BB1 is lying down (pitch over serial), upload, print the auto-arm state and gains after the reboot. Docs: FLASHING.md, TOOLS.md
- First run (Claude, testing the script; BB1 was on USB lying down at +89.6 deg) flashed the bias fix and the site clean-up: `Gyro calibrated`, `auto-arm: waiting`, gains v2 loaded

## 2026-10-24 - fix: no balance after switching on standing up

- **Bug:** switched on standing up (held by hand), the startup gyro calibration failed because the hand moves, and arming needed a successful calibration, so auto-arm waited forever (`waiting: keep still to calibrate`). Lying down it worked
- **Fix:** BB1 starts with `GYRO_BIAS_TYPICAL` (config.h: the average bias from every log, X within 0.15 deg/s), so it can arm without a calibration. Retries only while lying down and disarmed (a calibration stops the loop for 2 s). Arming no longer requires a calibration (`REASON_NOT_CALIBRATED` unused). `FLAG_NO_BIAS` now means "using the typical bias"
- `auto_state` 3 now means "no IMU" (LOG_FORMAT 8)

## 2026-10-24 - main page clean-up

- Main page (`page.h`) is now for using BB1, not tuning it: the Balance panel shows Balancing / Not balancing / Fallen and what auto-arm is waiting for, with **ARM** and **STOP** and a link to /tune. Gain boxes, the arm timer, the countdown bar and the step test are gone from it (all on /tune)
- ARM from the main page sends `heartbeat=0` (new `/arm` argument): no time limit, keeps balancing if the page closes. Hiding the main page no longer disarms. /tune keeps its timed runs with the heartbeat
- Developer panels (PID, Pitch, Log, Live values, Motor test, Sensors, Pico memory, Network) start folded (new localStorage key `bb1.folded2`, so old fold settings reset once)
- Docs: `handheld.py` marked as testing only (TOOLS.md, POWER_ON.md)

## 2026-10-24 - power-on behavior (auto-arm rework)

- Auto-arm moved out of `control.h` into **`auto_arm.h`**, a named-state machine (off / waiting / holding / balancing). Edge cases written up in **`docs/POWER_ON.md`**
- **Lift detector**: wheels over 4 turns/s for 0.5 s while armed = picked up, motors off (`REASON_LIFTED` = 12). Logs show under 2 turns/s on the floor
- **Repeated falls**: 3 falls within 30 s turn auto-arm off until ARM
- Auto-arm state on serial (`auto-arm: ...`), on both pages next to the mode, in `/state` (`auto_state`) and in the log (`auto_state`, LOG_FORMAT 7, LogRecord still 72 bytes). Mock updated
- Flashed 2026-10-24 (BB1 lying down: `auto-arm: waiting`, saved gains loaded)
- `tools/wiki_sync.sh`: copies `docs/` to the GitHub wiki (TOOLS.md)

## 2026-10-24 - auto-arm, repo clean-up

- MIT license; GitHub repo `hectorsvill/bb1-balance-bot-1` (private to start)

- **Auto-arm**: starts balancing by itself when stood upright and still for 1 s (after power-up and after a fall); STOP and other disarms turn it off until the next ARM. Setting `auto` (flash layout v4), /tune box, serial help. Firmware banner "step 8". Flashed 2026-10-24 (BB1 lying down: waits, not armed)
- GitHub-ready pass: README rewritten (status, how it balances, build, safety); `secrets.example.h` now has a placeholder AP password (`change-me-please`) and the docs no longer print one; absolute home paths replaced with `~`; `firmware/uf2/bb1.ino.uf2` rebuilt from the current firmware with the template password. Checked: no Wi-Fi password, email or keys in the files or git history (commits use the GitHub noreply address; `secrets.h` was never committed)

## 2026-10-24 - step 8: speed loop

- **Milestone: BB1 balances in place on wood** (round 27): P 18, I 100, D 0.5, sp 4, si 0.5, th 20, deadband 10, cap 200, trim 0. Wobble 0.2 deg, stays between the sticky notes, no turning. Saved to flash and made the `config.h` defaults

- Round 25: P 18 / I 100 / **D 0.5** / **sp 4**: wobble **0.2 deg**, no jitter, 3 of 3 to the timer, drift 2 to 4 turns. Saved to flash
- Heading hold `th` (`balanceSteer()`): equal wheel distance so it stops curving left; /tune box, flash layout v3, `/state`, log header `turn_hold`. Default off. Flashed 2026-10-24 (saved v2 settings loaded fine)

- **First real balance on wood: P 18, I 100, D 0.7** (round 23): 3 of 3 tries to the 10 s timer, wobble about 1 deg. The key: on the floor the motors act like speed knobs, so the angle loop needs a big I term (TUNING_LOG round 22). Saved to flash. Still drives forward and veers left: speed loop next

- `control.h`: outer speed loop moves the angle target: `target = trim - (sp x turns/s + si x turns since arming)`, limited to +-6 deg, wheel speed smoothed. New settings `sp` / `si` (serial, /gains, /tune boxes, flash layout v2, `/state`, log header `speed_p` / `speed_i`). Default 0 = off. Also a Deadband box on /tune. Firmware name "bb1 step 8". Flashed 2026-10-24; saved settings P 18, D 0.7, trim 0, cap 200, deadband 10, sp 1.5, si 0
- Rounds 15 to 19 on wood (TUNING_LOG): trim must be typed (hand-held "current pitch" came out +3.5), deadband 10 (15 chattered), D limit about 0.7 to 0.8 at 10 kHz, P 13 / 18 both ran away forward: the angle loop's limit

## 2026-10-24 - motor kit folder reviewed

- PWM frequency 20 kHz -> **10 kHz** (`PWM_FREQ`): wheels start at lower PWM, mostly forward (BUILD_RECORD). Flashed

- Battery calibrated against a meter (7.9 V vs 7.2 V): `BATTERY_DIVIDER` 11 -> 12.07. Pages, serial readout, handheld.py and pid_report show **volts per cell** (log column `batt_v` stays the whole pack; `# battery_cells` in the log header). Flashed 2026-10-24; reads 8.06 V (4.03 V/cell) after flashing

- Battery voltage: `battery.h` (core 0, GP26, D153C `ADC` pin = battery / 11), battery chip on `/` and `/tune` (green / orange < 7.0 V / red < 6.6 V), serial readout, `/state` fields, log column `batt_v` (LOG_FORMAT 6, LogRecord 72 bytes). Mock, handheld.py status line and pid_report (`batt` column) updated. Flashed 2026-10-24 with the clean-up and the 2 s heartbeat; RAM free 44.6 KB (heap used 13 KB). Reads 7.29 to 7.36 V running; the first reading after power-up came out wrong (5.2 V), fixed by discarding a few ADC samples at start (boot rows then read 7.7 V: to check against a meter)

- New docs/KIT_REFERENCE.md: the vendor folder has no balance code. The D153C schematic shows a built-in battery divider (VIN / 11 on the `ADC` header pin), so battery sensing is one wire to GP26, not a divider to build (FUTURE_INTEGRATIONS, WIRING updated; fixes the 100k/33k vs 100k/47k mismatch). Also: 65 C over-temperature cut-off on the 5 V (would reset the Pico), STBY has no pull resistor, TB6612 limits
- Knowledge graph of the repo with graphify (`graphify-out/`, added to .gitignore). It found 3 doc mismatches, all fixed: FIRMWARE_PLAN error sign (now pitch - setpoint, as built), FIRMWARE_PLAN heartbeat (now 2 s), battery divider values (now the D153C's own pin)

## 2026-10-23 (evening) - clean-up and plans

- Firmware: the balance settings' names and limits live in one place, `setBalanceSetting()` / `gainsLine()` in `settings.h`, used by serial, `/gains` and flash loading; limits as constants in `config.h`. The `/gains` answer is now the same line as serial `gains`. **Compiled, not flashed yet**
- Tools: one shared `post()` in `logger.py`; `logger.py` saves `# gains_vN` lines like handheld.py; `--surface` on both (`# surface,...` in the file)
- Wi-Fi heartbeat 1 s -> **2 s** (`HEARTBEAT_MS`): ~1 s Wi-Fi gaps cut good tries (round 10). Compiled with the clean-up, not flashed yet
- New docs/SESSION_LOG.md; TUNING_LOG "Plan"; FIRMWARE_PLAN section 6 "Data for training: status and rules"
- Rounds 12 to 14 on wood: balance point moved to about +0.55; deadband measured (10 to 15 in the air, about 30 on wood)

## 2026-10-23 - handheld.py picks the Pico's port

- New `tools/deadband_test.py`: sweeps motor-test PWM over Wi-Fi and finds where both wheels turn (round 14 showed the wheels don't move below about PWM 30 on wood)
- docs/TUNING_LOG.md: every tuning round, and what we do with the log data. Round 10 (no cable): never fell, 2 tries cut by Wi-Fi, wobble about 6 vs 3 with the cable
- pid_report.py names every disarm reason (Wi-Fi lost, tab hidden...) instead of "you"
- /tune: **Run a try** panel (seconds, ARM, STOP, countdown; space = STOP; hidden tab disarms), so BB1 can run with no USB cable
- handheld.py `--wifi`: no USB cable, arm / disarm / gains over Wi-Fi, heartbeat from its `/state` polling (0.2 s worst latency measured while logging)
- New `tools/pid_report.py`: one row per try (secs, end, wobble, lean, |P| |I| |D|, cap%, flips/s, turns) plus hints
- /tune: "Logs: record and read" section (needs a reflash to show)
- docs/PID_LESSON.md (PID for a balancing robot, FPV comparisons) and docs/PID_LEARNING_PROMPT.md (prompt for a separate learning chat)
- Run 2026-10-22 22:20: P 9 to 9.85 / D 0.74 / I 0 / cap 150 reached the timer 7 of 7 tries (wobble 0.4 to 4.2). Cap 45 and I 2 made most tries fall
- FLASHING.md: Arduino IDE 2 steps, the `No drive to deploy` fix (close the IDE Serial Monitor), two ports, settings survive a reflash. bb1 reflashed after `motor_test`; gains came back from flash
- Wiring diagram: `docs/wiring.svg` / `wiring.png`, drawn by `tools/make_wiring_svg.py`, shown at the top of WIRING.md
- Both motors dead: STBY wire (GP9) came unsoldered, re-soldered (BUILD_RECORD, Repair)
- `find_port()` in `serial_log.py` (used by handheld.py) picks the port with the Raspberry Pi USB vendor ID instead of the first `/dev/cu.usbmodem*`. An Anker dock had taken the first slot, so `a` went to the dock (`ERR: unknown cmd`)

## 2026-10-22 - Hector's tuning, floor run 5

- Run 8, same gains: 3 of 8 tries stood 10 s; rolls backward then falls. Likely the battery; drift needs step 8 (BUILD_RECORD, floor attempts 8)
- P 10 / D 0.75 to 0.85: **all 6 tries stood the full 10 s**. Left: slow ±10 deg swing while drifting (needs the step 8 speed loop) (BUILD_RECORD, floor attempts 7)
- P 10 / D 0.5 / trim −0.01: jitter gone, about 9.4 s up, slow growing overshoot (D term about 1/4 of P; deadband likely about PWM 30) (BUILD_RECORD, floor attempts 6)
- P 10 / D 5 / trim −0.58 / cap 150: about 3 s, overshoot and jitter. D term about 3x P and pinning the PWM at ±cap (BUILD_RECORD, floor attempts 5)

## 2026-10-21 - PID tuning page (/tune)

- New small page at http://bb1.local/tune (`tune_page.h`): Kp, Ki, Kd, trim, cap with Apply / Save to flash; live pitch, error, P / I / D, PWM out, 5 s chart; tuning notes. Never arms (arm and log with `handheld.py`). Also in `mock_bb1.py`
- Changing gains from a page now prints the `gains` line on serial (`printGains()` moved to `settings.h`), so `handheld.py` shows it and writes `# gains_vN,...` lines into the log
- docs/PID_TUNING.md: "Two ways to change the gains" (page, or `config.h` + `defaults` because flash overrides `config.h`)
- Gains renamed in the code for P / I / D: `gainKp` -> `gainP` (and I, D), `DEFAULT_KP` -> `DEFAULT_P_GAIN`, `Pid.kp` -> `Pid.gainP`; comments compare each to Betaflight. Serial also takes `p N` / `i N` / `d N`. Wire names (`kp` in the gains line, JSON, logs) unchanged. Flash layout unchanged (gains loaded fine after the update)
- /tune and PID_TUNING.md: which way to move the trim (rolls backward = raise it, forward = lower it)
- /tune and PID_TUNING.md: steps split into Start / Change a gain / Run a try / Finish (click the Terminal before pressing a)
- /tune: "How to use this page" rewritten, plus an "If you fly FPV" table
- /tune gain boxes labelled P (Kp), I (Ki), D (Kd), with a hover / tap helper saying what each does
- Checked on the robot: /tune loads, live data, Apply works and the gains line appears on serial

## 2026-10-21 - first floor attempts

- Run 4: cap 150 applied, longest try 7.2 s; rolls up to 2.6 wheel turns, so floor only (not a desk). Hector takes over the PID tuning from here

- Run 3: cap still 80 (the `:` command didn't reach BB1 twice). All 4 tries fell backward at the cap. handheld.py now takes `--kp --kd --cap --trim` at start and shows kp / kd / cap in the status line
- handheld.py: status line cut to the window width and redrawn only on change (a wrapped line was printing a new line every refresh)

- Run 2 (cap150 file, cap actually still 80): **first full 10 s standing**, then 6 short tries falling mostly backward at the cap. Average pitch suggests trim about −4.6. Next: cap 150 for real

- 3 tries at kp 15 / kd 0.5 / cap 80: about 2 s upright each, then fell backward once the PWM hit the cap (80). Details in BUILD_RECORD.md. Next: cap 150
- docs/HANDHELD_TOOL.md: how `tools/handheld.py` works inside

## 2026-10-21 - hand-held arm test: directions correct

- First full hand-held run (`tools/handheld.py`): armed 5 s with no Wi-Fi cut-out; leaning forward drove both wheels forward, leaning back drove them backward. Fall cut-out checked both ways (22:21 run). Numbers in BUILD_RECORD.md. Hand-held test passed: next is the floor

## 2026-10-21 - web server: close each connection (fix "lost the web page")

- Hand-held ARM kept disarming after 1 to 2 s with "lost the web page". The 21:04 log showed /state reaching BB1 only every 0.5 to 2.5 s although the page asks 20 times a second
- Cause: the WebServer library serves one connection at a time and, after answering, waits up to 5 s for the browser to close it. One connection left open froze every other request (measured: 4.8 s)
- Fix: web.h `route()` closes the connection after every answer. Re-measured: no more 5 s freeze. A long `/log` answer (Record) can still delay /state by a few hundred ms
- tools/handheld.py: the arm test in one window (a = arm 5 s, Enter = disarm, : = any serial command such as `kp 20`, q = save the log), records over Wi-Fi like logger.py. logger.py's file naming and saving moved into `log_path()` / `save_log()` so both share them
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
