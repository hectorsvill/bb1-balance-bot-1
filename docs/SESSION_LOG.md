# Session log

One entry per work session, newest first: what we did, what we learned, what's left. Tuning rounds have their own detail in [TUNING_LOG.md](TUNING_LOG.md); test numbers in [BUILD_RECORD.md](BUILD_RECORD.md); every change in [CHANGELOG.md](CHANGELOG.md). This page is the short story, so the next session can start without re-reading everything.

Template:

```text
## YYYY-MM-DD (evening): short title

**Did:**
**Learned:**
**Decided:**
**Open / next:**
```

## 2026-10-24 (afternoon): battery, 10 kHz, step 8, heading hold — BB1 balances

**Did:**
- Battery voltage on GP26 (D153C `ADC` pin), calibrated to a meter (12.07), shown per cell.
- PWM 20 -> 10 kHz (wheels start at lower PWM).
- Tuning rounds 15 to 27 on wood with exact values (Hector asked for them today).
- Firmware: step 8 speed loop (`sp`, `si`), heading hold (`th`), Deadband / sp / si / th boxes on /tune.

**Learned:**
- On the floor these motors act like **speed knobs**: wheel speed follows PWM. P/D alone can't balance that (always one unstable root); a **big I on the angle** (100) makes acceleration follow the lean. P becomes damping.
- The speed loop then stops the drift (sp) and holds position (si); heading hold fixes the stronger right wheel.
- Setting the trim by hand ("current pitch") is too rough; type it.

**Decided:** keep 10 kHz; new defaults in `config.h` = the round 27 settings.

**Result:** **BB1 balances in place**: 0.2 deg wobble, stays on the sticky notes, no turning.

**Also:** 30 s arms 3 of 3, and a push test passed (repeated taps, recovers from about 8 deg).

**Then:** auto-arm (balances by itself when stood up), and a GitHub-ready clean-up (README, placeholder AP password, no personal paths, rebuilt UF2, history checked for secrets).

**Then:** MIT license, pushed to https://github.com/hectorsvill/bb1-balance-bot-1 (private).

**Open / next:** Hector didn't like the code as it stands (auto-arm / latest changes): rework it in the milestone 2 thread. Auto-arm test (stand up, fall, stand up, STOP) not run yet. Change `BB1_AP_PASSWORD` in `secrets.h` before making the repo public. Then milestone 2: driving.

## 2026-10-24: knowledge graph, motor kit folder

**Did:**
- Built a knowledge graph of the repo with graphify (`graphify-out/graph.html`, 600 nodes, 32 clusters; gitignored).
- Reviewed the vendor kit folder (`~/Desktop/B570 balance trolley`); notes in `docs/KIT_REFERENCE.md`.
- Fixed 3 doc mismatches the graph found (error sign and heartbeat in FIRMWARE_PLAN, battery divider values).

**Learned:**
- The kit has **no balance code**, only motor demos and a wheel speed PI loop.
- **The D153C already has a battery divider** (VIN / 11 on its `ADC` header pin): battery voltage is one wire to GP26.
- The D153C's 5 V shuts off at about 65 °C, which would reset the Pico. STBY has no pull resistor on the board.

**Open / next:**
- Same as before: flash the clean-up firmware, then deadband, trim, P, D on wood.
- Hector is soldering the battery wire (D153C `ADC` to GP26). Firmware for it is written (`battery.h`, pages, log column `batt_v`), compiled, not flashed.
- Then test the PWM at 10 kHz vs 20 kHz with `deadband_test.py` (does the deadband shrink?).

## 2026-10-23 (evening): wood floor, trim moved, deadband, clean-up

**Did:**
- Tuning rounds 12 to 14 on the **wood floor**, no USB cable (TUNING_LOG).
- Measured the motor deadband with the new `tools/deadband_test.py`.
- Clean-up: firmware balance settings (names, limits, the "gains" line) in one place, `setBalanceSetting()` in `settings.h`, used by serial, the pages and flash loading (compiled, **not flashed yet**). Python: one shared `post()` in `logger.py`. `logger.py` now saves a `# gains_vN` line per gains version like `handheld.py`; both take `--surface`.
- Started this session log; wrote the tuning plan (TUNING_LOG, "Plan") and the data-for-training checklist (FIRMWARE_PLAN.md, section 6).

**Learned:**
- **The balance point moved** to about **+0.55 deg** (was −1 to −4.4), probably the battery or a wire shifting during the STBY repair. The trim follows where the weight sits.
- 0.05 deg of trim decided nothing: at 0.55 to 0.57 it fell both ways. BB1 is only *just* holding itself on wood.
- **Deadband:** wheels start at PWM 10 to 15 in the air, about 30 on wood. On wood BB1 sat 1.6 deg off for 1.7 s with PWM −15 to −19 and the wheels not moving. Small corrections do nothing until it's too late. Carpet and the cable had hidden this.
- `handheld.py` only saves the log on **q**; `pid_report.py` reads the newest *saved* file.

**Decided:**
- Tune on **wood** from now on (honest surface); note the surface in every log.
- Wi-Fi heartbeat 2 s instead of 1 s: Wi-Fi gaps of about 1 s were cutting good tries; the timer and the 45 deg cut-out still stop every run.
- Logs get their own git repo once BB1 balances.
- Every tuning round goes in TUNING_LOG; every session gets an entry here.

**Open / next:**
- Flash the clean-up firmware (needs USB once), check `gains`, `p 10`, Apply on /tune still work.
- Set `deadband` (Hector's pick, 15 to 30), re-find the trim, then P and D on wood (TUNING_LOG, "Plan").
- Wi-Fi heartbeat: **2 s** (Hector left it to Claude; changed in `config.h`; goes onto BB1 with the clean-up flash).
- Logs backup: Hector will make a git repo for the logs once BB1 balances well. Until then `logs/` is only on this laptop.

## 2026-10-23 (late night, early hours): motors, wireless, log reader

**Did:**
- Both motors dead: the **STBY wire came unsoldered**; re-soldered. Wiring diagram `docs/wiring.png`.
- `handheld.py` found the Anker dock's port; now picks the Pico by USB ID. Upload failed while the Arduino IDE Serial Monitor held the port (FLASHING.md).
- New `tools/pid_report.py` (one row per try plus hints), `docs/PID_LESSON.md`, `docs/PID_LEARNING_PROMPT.md`, `docs/TUNING_LOG.md`.
- **Cable-free runs**: `handheld.py --wifi`, ARM / STOP with a timer on /tune.

**Learned:**
- Best with the cable: P 9 to 9.85 / D 0.74 / I 0 / cap 150, 7 of 7 tries to the timer. Cap 45 and I 2 made it fall.
- The USB cable and carpet both damp BB1 and flatter the gains.
- Catching BB1 by hand spoils the data: press STOP first, or let it fall onto padding.

## 2026-10-22: tuning page, Hector starts tuning

**Did:**
- `/tune` page (gains, live P / I / D, notes), P / I / D naming with Betaflight comparisons, gain changes print on serial and go in the logs.
- Floor rounds 5 to 8 (TUNING_LOG): D 5 jitter, D 0.5 overshoot, D 0.75 to 0.85 stood 6 of 6.

**Learned:** D too high = jitter and PWM slamming ±cap; D too low = growing overshoot. Slow drift is the missing speed loop (step 8).

## 2026-10-21: bb1 firmware steps 1 to 7, first balance

**Did:**
- Firmware steps 1 to 7: pitch, two cores, Wi-Fi page, logging, motors, arm / PD loop, saved settings and step test. Fixed "lost the web page" (close each connection).
- Hand-held test passed; first floor tries, first 10 s standing (P 15 / D 0.5).

**Learned:** IMU signs (forward = +), trim about −4, cap 80 was too low to catch it.

## 2026-10-20: build and bring-up

**Did:** built Milestone 1 (Pico W, D153C, MPU-6050, chassis, printed frame); every part passed its bring-up test; signs and directions measured (CLAUDE.md, BUILD_RECORD).
