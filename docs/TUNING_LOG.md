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

## Plan: how we get BB1 to balance on wood

Where we are (2026-10-23): wood floor, no cable. Balance point about +0.55 deg. Deadband about 15 to 30 on wood. P 9.85 / D 0.74 only *just* holds it: tries fall within 1 to 3 s, either way.

Each step is one round (3+ hands-off tries, `pid_report.py`, an entry below). Hector picks the numbers; the report says which way to move.

1. **Make the robot repeatable first.** Battery strapped in the same spot every time (the balance point follows it). Battery charged (write the voltage in the `-n` note until the voltage divider is fitted). Same surface, `--surface wood`.
2. **Deadband.** Set `deadband` on /tune. Check in the report or `--plot`: small errors now move the wheels (no long stretches with PWM under 30 and speed 0). Too high = fast chatter around upright: back it off.
3. **Trim again, with the deadband in.** Halving method: falls forward = lower, falls back = raise. Stop when it falls both ways about equally; 0.1 deg is fine enough. **Save to flash.**
4. **P.** Raise P until it fights back hard and starts a fast shake, then come back down a bit. That gives the strongest P that doesn't oscillate on its own. Watch `|P|` and `cap%`: P maxing the motors means the cap is in the way.
5. **D.** Raise D until the swings stop growing and die out; stop before jitter (`flips/s` in the tens, `|D|` bigger than `|P|`). Then go back to step 4 once: more D usually lets P go a bit higher.
6. **Compare with the step test** (full page, armed): setpoint +2 deg for 1 s. A good PD settles in well under a second with one small overshoot.
7. **Accept the drift.** PD on the angle can stand 10 s but still roll away slowly: it has no idea where the wheels are. That's not a gain problem.
8. **Step 8: the speed loop** (cascade, like position hold on a quad). An outer loop reads the wheel speed from the encoders and tilts the angle setpoint a little to stop the rolling; the angle PD stays the inner loop. I (if any) goes there, not on the angle. This is what fixes "starts moving back, then falls".

If D can't go high enough without jitter, the next tools are a low-pass filter on the D term, or checking the gyro noise; both are firmware changes we'd plan together.

## Rounds

### 2026-10-24, round 21: speed loop on, sp 1.5 (`bb1_2026-10-24_16-26-15_sp1-5-p18-d0-7.csv`)

**Setting:** P 18, D 0.7, trim 0, cap 200, deadband 10, **sp 1.5**, si 0, wood, 3.96 V/cell.

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 3.1 | FELL fwd | 6.5 | +4.3 | 83 | 30 | 8 | 14.0 | +6.1 |
| 2 | 2.4 | FELL fwd | 6.9 | +5.5 | 101 | 38 | 11 | 12.9 | +6.1 |
| 3 | 2.6 | FELL fwd | 6.8 | +5.2 | 93 | 14 | 10 | 1.5 | +6.2 |

**What it says:** the speed loop works in the intended direction: as BB1 rolled forward the target moved back (setpoint 0 -> −2.7 deg at about 2 turns/s), which adds forward push to get the wheels ahead. A little longer than round 19 (2.4 to 3.1 s vs 1.8 to 2.3) but the same forward runaway: the shift was too small and too late (only −1 deg by the time the lean was +3). Wheel acceleration on the floor was only about 1.4 turns/s², which can only hold a lean of about 2 deg, so the corrections have to come early. **Next:** sp **4** (a clearer test: better = the loop helps, worse = the sign is wrong).

### 2026-10-24, round 19: P 18, D 0.7, trim 0.4, cap 200 (`bb1_2026-10-24_16-15-14_p18-d0-7-trim0-4-cap200.csv`)

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2.3 | FELL fwd | 6.4 | +4.8 | 86 | 16 | 10 | 2.6 | +5.1 |
| 2 | 2.2 | FELL fwd | 6.7 | +4.7 | 90 | 34 | 11 | 13.3 | +4.7 |
| 3 | 1.8 | FELL fwd | 6.9 | +5.8 | 104 | 21 | 13 | 0.0 | +4.6 |

**What it says:** same shape as round 18 even with P 18: the lean grows slowly 1 -> 10 deg over 2 s while the wheels speed up steadily, until the cap. More P didn't change it. This is the angle loop's limit, not a gain problem: any small trim error is a steady lean, and a steady lean means accelerating forever (no idea it's rolling). That also explains why 0.05 deg of trim flipped the fall direction. **Decision: build step 8, the speed loop, now** (firmware README, PID_LESSON 7b). Meanwhile a trim 0.0 check (round 20).

### 2026-10-24, round 18: P 13, D 0.7 (`bb1_2026-10-24_16-11-55_p13-d0-7-db10-trim0-6.csv`)

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1.5 | FELL fwd | 7.4 | +6.5 | 84 | 32 | 15 | 6.7 | +3.1 |
| 2 | 2.1 | FELL fwd | 7.0 | +4.5 | 62 | 20 | 11 | 5.2 | +3.1 |
| 3 | 1.9 | FELL fwd | 7.0 | +5.1 | 67 | 20 | 13 | 3.2 | +3.1 |
| 4 | 1.9 | FELL fwd | 7.1 | +4.6 | 66 | 42 | 11 | 15.1 | +2.9 |

**What it says:** jitter mostly gone. Every try did the same thing: a slow, steady forward lean (about 2 to 5 deg/s) while the wheels sped up forward, PWM rising with the lean, but the lean never stopped growing; at about 10 deg the PWM hit the cap and it fell. To stop a lean the wheels must *accelerate* under it, and a motor's PWM sets roughly its speed, not its push: as the wheels speed up, less of the PWM is left to accelerate them. So the push per degree (P) is still too small, and near the end the cap ran out. All 4 fell forward, so the trim is a bit forward too. **Next:** P **18**, D 0.7, trim **0.4**, cap **200**.

### 2026-10-24, round 17: P 13, D 1.0 (`bb1_2026-10-24_16-08-52_p13-d1-0-db10-trim0-6.csv`)

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1.2 | FELL fwd | 7.6 | +7.9 | 103 | 80 | 32 | 11.9 | +2.7 |
| 2 | 1.5 | FELL fwd | 7.6 | +6.6 | 87 | 82 | 30 | 14.7 | +2.9 |
| 3 | 1.7 | FELL back | 7.0 | −6.8 | 89 | 84 | 33 | 15.2 | −3.4 |

**What it says:** worse. The gyro rate swings ±100 to 160 deg/s at about **13.5 Hz** while the pitch only moves a few degrees: a D-term oscillation (wheels kick, body jerks, gyro spikes, D kicks back), like round 5 but smaller. D 1.0 is past the limit here; 0.74 was under it. The deadband (±10 on every sign change) and the stronger low-PWM torque at 10 kHz both make the loop more aggressive, so the D limit is lower than on the cable runs. **Next:** keep P 13, D back to **0.7**. If D stays capped this low, a low-pass filter on the D term (firmware) is the tool to allow more.

### 2026-10-24, round 16: trim typed 0.5, deadband 10 (`bb1_2026-10-24_16-05-52_db10-trim0-5-p9-85-d0-74.csv`)

**Setting:** P 9.85, I 0, D 0.74, trim 0.50 (typed), cap 150, deadband 10, 10 kHz, wood, 3.95 V/cell.

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2.4 | FELL fwd | 6.9 | +4.9 | 49 | 17 | 9 | 1.2 | +2.9 |
| 2 | 2.9 | FELL back | 6.5 | −3.4 | 36 | 15 | 7 | 3.1 | −2.8 |
| 3 | 2.8 | FELL back | 6.6 | −3.2 | 37 | 15 | 7 | 2.1 | −2.8 |

**What it says:** better: twice as long, jitter gone (1 to 3 flips/s), falls go both ways (trim about right). Try 2 in detail: it held within ±1 deg for about 1.5 s with the wheels moving (deadband works), then leaned slowly back; at −5 deg P gave only about −90 PWM, the wheels sped up backward but never got back under it, the lean grew to −10 deg and it fell. That's **P too weak** to recover a lean once it starts. **Next:** P **13**, D **1.0** (raised together, same D/P ratio about 0.075, so the damping stays similar), trim 0.6.

### 2026-10-24, round 15: deadband 15, 10 kHz PWM, wood (`bb1_2026-10-24_16-01-41_db15-p9-85-d0-74.csv`)

**Setting:** P 9.85, I 0, D 0.74, cap 150, **deadband 15**, PWM 10 kHz, battery 3.92 V/cell. Trim came out **+3.46** from "Trim = current pitch" (expected about +0.5).

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1.3 | FELL fwd | 8.4 | +6.3 | 65 | 44 | 15 | 12.9 | +2.2 |
| 2 | 1.3 | FELL fwd | 8.6 | +5.4 | 63 | 57 | 15 | 14.2 | +2.0 |
| 3 | 1.4 | FELL fwd | 8.6 | +5.0 | 62 | 45 | 14 | 15.4 | +2.0 |

**What it says:** trim +3.46 is about 3 deg forward of the balance point, so all 3 fell forward in about 1.3 s (same as round 12). The PWM also flipped direction 13 to 15 times a second: with a deadband every tiny sign change of the output becomes a jump from +15 to −15, which is chatter. **Next:** trim typed as **0.5** (not held by hand), deadband **10**, same P / D.

### 2026-10-23, round 14: trim search 0.5 to 0.7, wood (`bb1_2026-10-23_21-13-20_wood-trim-4.csv`)

**Setting:** P 9.85, I 0, D 0.74, cap 150, wood, no cable. Trim tried 0.5, 0.7, 0.55, 0.58, 0.57 (v6 to v10).

| gains | trim | secs | end | wobble | lean | \|P\| | \|D\| | cap% | turns |
|---|---|---|---|---|---|---|---|---|---|
| v6 | 0.50 | 2.0 | FELL back | 7.0 | −6.3 | 62 | 19 | 11 | −3.0 |
| v7 | 0.70 | 1.3 | FELL fwd | 7.8 | +7.9 | 78 | 34 | 18 | +2.4 |
| v7 | 0.70 | 1.2 | FELL fwd | 7.8 | +8.5 | 83 | 33 | 19 | +2.3 |
| v8 | 0.55 | 3.0 | FELL fwd | 7.0 | +2.1 | 41 | 14 | 8 | +2.3 |
| v8 | 0.55 | 1.3 | FELL fwd | 8.0 | +7.4 | 73 | 31 | 18 | +2.3 |
| v9 | 0.58 | 1.4 | FELL fwd | 7.8 | +7.6 | 75 | 31 | 16 | +2.3 |
| v10 | 0.57 | 10.0 | timer | 6.1 | −0.1 | 47 | 19 | 1 | −0.6 |
| v10 | 0.57 | 2.6 | FELL back | 6.6 | −8.1 | 80 | 25 | 10 | −5.1 |

**What it says:**
- The balance point is about **+0.55**. But 0.05 deg of trim can't really decide forward vs back: a trim that fine is below what the IMU and your hands can repeat. Which way it fell depends on how each try started, which means BB1 is only **just** holding itself, not that the trim is wrong.
- **The real problem is the motor deadband.** In one try BB1 sat 1.6 deg off the trim for 1.7 s with the PWM at −15 to −19 and the **wheels not turning at all** (speed 0). When it then tipped forward, the wheels only started moving once the PWM passed about 30, and by then it was already falling at 12 deg/s. So small corrections do nothing on wood, BB1 drifts until the error is big, then it's too late. Carpet and the cable hid this by holding it still.
- The firmware has a `deadband` setting for exactly this (`control.h`: it adds that much PWM to every correction so small ones still turn the wheels). It's 0 now.

**Next:** measure the deadband (motor test at rising PWM until the wheels move), set `deadband` to it, keep trim 0.55 and the same P / D.

**Deadband measured** (`logs/deadband_2026-10-23_21-37-35.csv`, `tools/deadband_test.py`, wheels in the air, 1 s per test):

| PWM | 5 | 10 | 15 | 20 | 30 | 40 | 60 |
|---|---|---|---|---|---|---|---|
| left / right counts fwd | 0 / 1 | 0 / 2 | 100 / 124 | 190 / 215 | 367 / 401 | 550 / 591 | 923 / 975 |

- In the air the wheels start between **PWM 10 and 15**, the same both directions. Above that, speed rises in a straight line (about 18 counts per PWM step of 1).
- On wood, round 14 showed the wheels stuck at PWM 15 to 19 and starting near **30**: the robot's weight on the wheels and the floor add friction. So the floor deadband is somewhere around **15 to 30**.
- The right wheel turns about 7% faster than the left at the same PWM. Doesn't matter for balancing; it will for driving straight (milestone 2).

### 2026-10-23, round 13: trim −4.44, wood (`bb1_2026-10-23_21-04-58_wood-p9-85-d0-74-trim-fix.csv`)

**Setting:** P 9.85, I 0, D 0.74, trim −4.44 (the value in flash), cap 150, wood floor, no cable.

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1.0 | FELL back | 9.5 | −6.5 | 76 | 43 | 21 | 4.9 | −1.8 |
| 2 | 0.9 | FELL back | 8.9 | −7.5 | 78 | 44 | 22 | 1.1 | −1.8 |
| 3 | 1.3 | FELL back | 9.1 | −4.4 | 65 | 34 | 16 | 0.8 | −1.8 |

**What it says:**
- Every try was armed with BB1 held at about **+1 deg** pitch (where it looked upright), 5 deg ahead of the trim. P drove the wheels forward (PWM about 51), which tipped the body back, and it kept falling back at a steady 25 to 30 deg/s until the cap. Same story all 3 times.
- Round 12 (trim +6.1) fell **forward**; this round (trim −4.44) fell **back**. So the real balance point is now **between −4.4 and +6.1**, and BB1's own "upright" reading of about +1 suggests near 0 to +1. It has **moved** from the −1 to −4.4 measured before: probably the battery or a wire shifted during the STBY repair (the balance point follows where the weight sits).
- P and D can't be judged until the trim is right.

**Next:** find the trim by halving: try a value in the middle, see which way it falls, move halfway toward the other side. Check the battery strap first.

### 2026-10-23, round 12: trim set to +6.1 (`bb1_2026-10-23_20-45-04_carpet-p9-85-d0-74.csv`)

**Setting:** P 9.85, I 0, D 0.74, **trim +6.1**, **cap 100**, **wood floor** (the file name says carpet, but it was wood), no cable.

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0.6 | FELL fwd | 9.4 | +8.4 | 82 | 61 | 37 | 0.0 | +0.9 |
| 2 | 0.8 | FELL fwd | 10.2 | +5.5 | 75 | 57 | 29 | 6.1 | +0.9 |
| 3 | 0.7 | FELL fwd | 9.9 | +7.3 | 80 | 61 | 35 | 1.5 | +0.9 |

**What it says:** the trim jumped from −4.44 to **+6.1** during the round (probably "Trim = current pitch" clicked while BB1 was tilted forward). With the target forward of where it really balances, BB1 is told to hold a forward lean, gravity takes it forward, and it falls forward in under a second every time. Says nothing about P or D. (The report's trim hint gave the wrong direction for a "+" lean; fixed: falls forward = lower the trim.)

**Next:** re-set the trim at the real balance point (expect about −1 to −4), cap back to what worked (150), same gains, 3 tries.

### 2026-10-23, round 11: carpet, hands off (`bb1_2026-10-22_23-02-25_p9-85-d0-74.csv`)

**Setting:** P 9.85, I 0, D 0.74, trim −2.60, cap 150. No cable, armed over Wi-Fi, **on carpet**, no hand help.

| try | secs | end | wobble | lean | \|P\| | \|D\| | cap% | flips/s | turns |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 2.2 | FELL back | 8.4 | −6.5 | 79 | 22 | 17 | 0.5 | −3.5 |
| 2 | 10.0 | timer | 1.2 | +0.5 | 11 | 3 | 0 | 1.4 | 0.0 |
| 3 | 10.0 | timer | **0.3** | −1.0 | 10 | 1 | 0 | 0.2 | 0.0 |

**What it says:**
- **Tries 2 and 3 are the best balancing so far:** ±0.3 to 1.2 deg, P doing only about 10 PWM of work, no rolling. Carpet's rolling resistance helps a lot (it damps drift, like the cable did), so these numbers are carpet-only.
- **Try 1 started badly:** armed at about +9 deg from the trim (the limit is 10), so P shoved hard forward (PWM 93), it overshot to −7 deg and then **sat leaning back at −7 to −9 deg for about a second** while rolling backward faster and faster. At that lean P only gave about −55 PWM: not enough to drive the wheels back under it, so the lean grew until the motors hit the cap and it fell. A steady lean like that is P being too weak for a big error (or the missing speed loop letting it run away).
- The report's hints ("more D", "drift") come from try 1 only; tries 2 and 3 need nothing.

**Next:** arm closer to upright (within about ±3 deg of trim). Then either try a little more P (to recover from big leans like try 1) or move to a hard floor, where the robot is honest. Same surface for every comparison.

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
