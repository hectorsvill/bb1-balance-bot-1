# Tuning log

The running record of Hector's PID tuning on BB1: what we tried, what the data said, what we changed next. Newest round first. Detailed numbers for older runs are in [BUILD_RECORD.md](BUILD_RECORD.md); the theory is in [PID_LESSON.md](PID_LESSON.md).

## What we're doing with the data

**Goal now:** find the angle-loop gains (P, I, D), trim and cap that keep BB1 upright the longest with the least rocking. Hector picks the gains; the logs say what each change did.

**Where the data comes from:**
1. BB1 runs its balance loop 200 times a second. Every tick it stores one record: pitch, pitch rate, the trim, the P / I / D terms, PWM to each motor, wheel encoders, the gains version, why it disarmed, how long since the last Wi-Fi `/state` (`web_age_ms`). Columns: [CALIBRATION_AND_LOGS.md](CALIBRATION_AND_LOGS.md).
2. BB1 keeps the last 10 s in memory. `handheld.py` (or `logger.py`) pulls new records over Wi-Fi a few times a second and saves them to `logs/bb1_<date>_<time>_<note>.csv` when you press **q**. Every gains setting used during the run is written at the top as a `# gains_vN` line.
3. `tools/pid_report.py <file>` splits the log into tries (each arm) and gives one row per try.

**How we use it, each round:**
1. Run 3 or more tries at one setting.
2. Read the report. A try only counts for comparing gains if it ended by **timer** or **FELL** with **no hand touching it**: "Wi-Fi lost", "you" and "tab hidden" ends say nothing about the PID. To save BB1 from a fall, press **STOP** (space) *before* catching it, so the try ends as "you" and your push isn't counted as the PID's work. Better still: soft padding around it and let it fall; the 45 deg cut-out stops the motors.
3. Compare settings by **wobble** (smaller = calmer) among tries that reach the timer. Use `cap%`, `flips/s`, `|P|` vs `|D|`, `lean` and `turns` to see *why* (table in PID_LESSON.md, section 7).
4. Change one thing, write the round down here (setting, report, what it means, next change).

**Later:** every log is kept. They're the training data for the AI model that will drive BB1 (FIRMWARE_PLAN.md, section 6): good tries show what "balanced" looks like, falls show what to avoid. That's why the gains version and the disarm reason are in every row.

## Rounds

### 2026-10-23, round 10: first run with no USB cable (`bb1_2026-10-22_22-55-20_p9-85-d0-74.csv`)

**Setting:** P 9.85, I 0, D 0.74, trim −2.60, cap 150. Armed over Wi-Fi from the /tune page, USB unplugged.

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 10.0 | timer | 6.0 | +0.1 | 49 | 17 | 0 | 2.5 | +0.2 |
| 2 | 6.7 | Wi-Fi lost | 4.7 | −0.3 | 38 | 13 | 0 | 1.6 | −0.2 |
| 3 | 1.2 | Wi-Fi lost | 1.8 | −2.4 | 25 | 13 | 0 | 2.6 | −0.2 |
| 4 | 10.0 | timer | 5.9 | +0.3 | 51 | 20 | 0 | 3.0 | +1.0 |

**What it says:**
- No falls in the log, but **Hector was catching it and pushing it back** when it was about to fall. So "timer" here doesn't mean it balanced alone, and the wobble includes his pushes. Tries with hand help can't be compared fairly with hands-off tries.
- Tries 2 and 3 were **cut by Wi-Fi**: no `/state` for 1 s (`web_age_ms` hit 1000), so the heartbeat disarmed it. Not the PID. (The first report version called these "you"; `pid_report.py` now names them.) Try 1 also came close (980 ms).
- **Wobble about 6 deg without the cable vs 2.6 to 4.2 with it** (same P and D, round 9). The cable was holding it a little, like a light tether, and damping the swing. Without it BB1 swings more, |P| doubles (about 50 vs 25): the gains now feel the real robot.
- Motors never at the cap, little jitter: D isn't too high.

**Next:** decide how to stop Wi-Fi cutting tries (longer heartbeat, see below), then tune D for the cable-free robot (more braking for the bigger swing is the direction to explore).

### 2026-10-22, round 9: cap 45 and I 2 (`bb1_2026-10-22_22-20-29_p10-d0-75.csv`)

P 9 to 9.85 / D 0.74 / I 0 / cap 150 / trim about −1: **7 of 7 tries reached the timer**, wobble 0.4 to 4.2 (with USB). Then cap 45: motors at the cap 10 to 75% of the time, falls. Then I 2: worse. Lesson: the cap must be high enough to catch it; I isn't needed with a good trim.

### 2026-10-22, rounds 5 to 8 (BUILD_RECORD, floor attempts 5 to 8)

- P 10 / D 5: jitter, D term 3x P, PWM slamming ±150 25 times a second. D far too high.
- P 10 / D 0.5: jitter gone, 9.4 s, slow growing overshoot. D too low.
- P 10 / D 0.75 to 0.85: **6 of 6 to the timer**. Slow ±10 deg swing while drifting: needs the speed loop (step 8).
- Same gains later: rolled backward and fell more; likely the battery getting low, then the STBY wire came loose (2026-10-23).

### 2026-10-21, rounds 1 to 4 (BUILD_RECORD, floor attempts 1 to 4)

P 15 / D 0.5: about 2 s at cap 80 (falling at the cap), first 10 s stand, 7.2 s at cap 150. Rolled up to 2.6 wheel turns: floor only.
