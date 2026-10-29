# PID Tuning Guide for BB1

Short version, the why: [PID_LESSON.md](PID_LESSON.md). Read a log: `tools/venv/bin/python tools/pid_report.py` (newest log, `--plot` to draw). Every round, what the data said and what changed next: [TUNING_LOG.md](TUNING_LOG.md). A prompt for a separate learning chat: [PID_LEARNING_PROMPT.md](PID_LEARNING_PROMPT.md).

How the balance controller works, how to tune it on the real robot with the web page, and where to read more.

## 1. The idea in one paragraph

A balancing robot is an upside-down pendulum. If it leans forward, the wheels must roll forward to get back under it, the same way you balance a broom on your hand. The controller looks at how far it leans (the error) and how fast it is falling, and turns that into a motor command.

## 2. The three terms

```text
error  = pitch - setpoint          (setpoint = the balance trim; forward lean past it = positive)
P term = Kp * error                 how far am I leaning?   -> drive harder the more I lean
I term = Ki * sum(error * dt)       have I been leaning for a while? -> fix a steady offset
D term = Kd * pitchRate             how fast am I falling?  -> react to the fall early, damp overshoot
pwm    = P + I + D                  (plus the deadband, clamped to the PWM cap), same for both wheels
```

Signs on BB1 (as built in step 6, `control.h`): pitch and pitch rate are positive leaning forward, and positive PWM rolls both wheels forward. Leaning forward, the wheels must roll forward to get back under the robot, so the error is `pitch - setpoint` and all three gains are positive. (Many textbooks write `setpoint - pitch` with a minus sign elsewhere; it's the same controller.)

- **P** does most of the work. Too little: it falls slowly in one direction. Too much: it shakes back and forth fast.
- **D** is the brake on P. It reacts to speed, not position, so it calms the oscillation. We use the gyro rate directly (it *is* the derivative of the angle), which is much less noisy than subtracting two angles.
- **I** cleans up a steady lean (for example the battery not being centered). Small, and clamped (anti-windup) so it can't build up while the robot lies on the floor.

Later the **speed loop** sits outside this one: if the wheels keep rolling forward, it tilts the setpoint back a fraction of a degree so the robot stops drifting.

## 3. Before tuning

- Pitch angle steady and correct sign (step 1 of the firmware plan).
- Find the balance point: hold the robot where it feels balanced, read the pitch, and set it as the trim.
- Find the motor deadband: the PWM at which the wheels just start to move. The controller adds it so small commands aren't lost.
- PWM cap low (150/255) to start. Something soft to fall on.

On BB1 (step 6) these are all on the page's **Balance** panel or serial: `trim here` / **Use current pitch as trim**, `deadband N`, `cap N`, `kp N`, `ki N`, `kd N`. **Save to flash** (or `save`) keeps them after a restart (step 7); **Defaults** goes back to `config.h`. Each change bumps `gains_ver`, which is in every log row.

### The hand-held test (step 6): what it's for

Before BB1 ever balances on the floor, one thing must be true: **when it leans, the wheels drive toward the lean**, the way you move your hand under a falling broom. If a sign anywhere is wrong (IMU, motors, the controller), the wheels drive the other way and push it over faster. On the floor that looks like "it just falls", and you can't tell a sign error from bad gains. Held in the air, it's obvious and safe.

What you do and what each part checks:

| Do | Expect | Checks |
|---|---|---|
| Hold it at its balance point, **Use current pitch as trim** | the trim box shows the angle (−4.39 deg on 2026-10-21) | the setpoint is the real balance point, not 0 |
| cap 80, Apply, hold it in the air, **ARM** | ARMED (green) | arming works and the motors stay gentle |
| tilt a little forward ("hectorsvill" side down) | both wheels spin forward, faster with more tilt | the whole sign chain: pitch + -> error + -> PWM + -> wheels forward |
| tilt a little back | both wheels spin backward | same, the other way |
| tilt past 45 deg | motors stop, FALLEN | the fall cut-out works |
| Record during all of it, Download, `check_dump.py --plot` | pitch, P/D terms and PWM move together | the log shows what you saw, for later analysis and training |

**How to run it now:** arm from serial and record with `logger.py`, page closed. Step by step: `firmware/README.md`, "Hand-held test, the current way". The table above still says what each part checks.

Hand-held, it won't feel like balancing: nothing pushes back on the wheels, so they just spin. That's expected; this test is only about direction and the cut-outs.

### Names: P, I, D vs Kp, Ki, Kd

Same thing, two habits. Betaflight calls the gains **P, I, D**; textbooks call them **Kp, Ki, Kd** ("K" = gain). The page shows both, e.g. **P (Kp)**. In the code the gains are `gainP`, `gainI`, `gainD` (`shared.h`, `pid.h`) and the parts of the output they make are the **P term, I term, D term** (`p`, `i`, `d` in `pid.h`, `p_term`... in the log). Serial takes `p 15` or `kp 15` (same for i / d). The `gains` line, `/state` JSON and log headers still say `kp ki kd` so old logs and tools keep working.

| Term | On a quad (Betaflight) | On BB1 |
|---|---|---|
| P | snaps back to the stick; too much = fast P wobble | pushes against the lean; too much = fast shaking |
| I | holds the angle against wind or a heavy side | fixes a steady lean one way; too much = slow rocking |
| D | damps bounce-back and prop wash; too much = hot motors | brakes the fall; too much = buzzing, jittery motors |
| Trim | accelerometer trim | the angle where BB1 really balances |
| Cap | motor output limit | max PWM while balancing (of 255) |

The big difference: a quad's setpoint comes from your stick; BB1's is fixed at "upright" (the trim). One axis only.

### Two ways to change the gains

**A. The tuning page (live, no flashing).** Open **http://bb1.local/tune** (or `http://<ip>/tune`; type `wifi` on serial for the IP). It shows only what tuning needs: boxes for Kp, Ki, Kd, trim and cap, live pitch, error, the P / I / D terms, PWM out, a 5 s chart, and short notes.

**Start (once)**
1. Battery on (D153C switch). **No USB cable**: it tugs on BB1 and changes the result.
2. Terminal: `tools/venv/bin/python tools/handheld.py --wifi -s 10 -n "P10 D0.74"` (no `--kp/--kd/--cap`: they would overwrite the page). With USB instead: leave out `--wifi`, battery on first, then USB.
3. **Expect:** bottom line `pitch … (trim …) | kp 15 kd 0.5 cap 80 | log 0 s`.
4. Browser: open **http://bb1.local/tune**. Put the browser and the Terminal side by side.

**Change a gain**
1. Page: change **one** box, click **Apply**.
2. **Expect:** Terminal prints `BB1: gains vN: kp …` with your new number.

**Run a try**
1. Stand BB1 upright on the floor, hands close but not touching. The status line `(trim …)` must be within ±10.
2. **Click the Terminal window** (keys only work there, not in the browser).
3. Press **a**. **Expect:** `ARMED`.
4. Let go and watch. It stops by itself after 10 s or when it falls past 45 deg. **Enter** stops it early.
5. Catch it. Do steps 1 to 5 three times, then go back to "Change a gain".

**Finish**
1. Liked a setting: page **Save to flash** (disarmed only).
2. Terminal: press **q**. **Expect:** `Saved … records … to logs/bb1_…csv`.
3. Send the file name and what it did.

You can arm from the page (**Run a try**: seconds, **ARM**, **STOP** / space bar) or from the terminal (**a**). Armed over Wi-Fi (page or `--wifi`), BB1 disarms if Wi-Fi goes quiet for 2 s; armed over USB it doesn't. Every change prints a `gains` line on serial, and `handheld.py` writes each version into the log file as `# gains_vN,kp ... cap ...`, so the log says which gains every row used (`gains_ver` column).

**B. In the code (`config.h`).** The starting values are `DEFAULT_P_GAIN`, `DEFAULT_I_GAIN`, `DEFAULT_D_GAIN`, `DEFAULT_TRIM`, `DEFAULT_PWM_CAP` in `firmware/bb1/config.h`.

1. `vim firmware/bb1/config.h`, change the number, save.
2. Compile and upload (`docs/FLASHING.md`), about a minute.
3. **Important:** saved settings in flash **override** `config.h` at startup. After flashing, type `defaults` on serial (or `:` → `defaults` in `handheld.py`), then `save`. **Expect:** `gains vN: ...` shows your new numbers.

Without step 3 your code change seems to do nothing. Use A while tuning (seconds per change); use B to write down the final numbers so a fresh flash starts from them.

## 4. Tuning steps on BB1

All gains change live from the tuning page (above) or serial. Change one thing at a time and write down what you saw.

1. **Kp only** (Ki = 0, Kd = 0). Start small and raise it until the robot fights back when tilted and starts to oscillate fast around upright. That's roughly "too much".
2. **Add Kd.** Raise it until the oscillation calms down. Too much D makes the motors buzz or jitter (it amplifies noise). Then try raising Kp a bit more, and Kd again.
3. **Add a little Ki** only if it keeps leaning or drifting one way. Small steps.
4. **Speed loop** (later step): raise its gain until the robot stops wandering, but not so much it rocks slowly back and forth.
5. **Save** the gains (disarmed) and log a run with the gains in the note.

6. **Fast driving (firmware 3.4):** once it drives well at the defaults, try `aff` (acceleration lean, about 1.0 by physics: 1 turn/s per s needs 1.04 deg of lean), then a faster ramp `dacc`, then a bigger lean limit `lmax`. One at a time, same drive test each time. Quad picture: `aff` is Betaflight feedforward for the speed loop (lean when the stick moves, not after the error builds), `lmax` is the angle-mode max tilt. In the log, the setpoint should move before the pitch does, and `pwm_l`/`pwm_r` should stay well under the cap.

Use the **step test** (setpoint +2 deg for 1 s) to compare gains: a good response settles in well under a second with one small overshoot.

| You see | Likely cause | Try |
|---|---|---|
| Falls over slowly one way | Kp too low, or trim wrong | raise Kp, check trim |
| Fast back-and-forth shaking | Kp too high, Kd too low | more Kd or less Kp |
| Buzzing, jittery motors | Kd too high or noisy gyro | lower Kd |
| Slow rocking, big swings | I or speed loop too strong | lower Ki / speed gain |
| Leans steadily, rolls off | balance trim off (BB1 holds the trim angle; if that isn't the real balance point, gravity keeps pulling it that way) | rolls **backward**: raise trim (toward +); rolls **forward**: lower trim (toward −); about 0.5 deg steps. Later a small Ki |
| Wheels spin but it still falls | PWM cap too low (saturating) | check the saturated flag, raise the cap |

> Classic Ziegler-Nichols tuning (raise Kp until it oscillates steadily, then compute the gains from that) is worth reading about, but hand tuning as above is the normal way for balancing robots.

## 5. Reading and videos

Start with the first two, they're the best value.

| What | Why read it |
|---|---|
| Brett Beauregard, "Improving the Beginner's PID" blog series ([brettbeauregard.com](http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/)) | How a PID is actually written in Arduino code: sample time, derivative kick, windup. Our `pid.h` follows it. |
| Brian Douglas, "Understanding PID Control" (MATLAB Tech Talks, [mathworks.com](https://www.mathworks.com/videos/series/understanding-pid-control.html)) | Short videos that build intuition for P, I, D, noise and filtering. |
| Shane Colton, "The Balance Filter" (MIT, 2007; [web.mit.edu/scolton/www/filter.pdf](http://web.mit.edu/scolton/www/filter.pdf), may have moved) | The complementary filter for exactly this kind of robot, in a few pages. |
| Tim Wescott, "PID Without a PhD" (Embedded Systems Programming, 2000; search the title, free PDF) | Practical PID for embedded engineers. |
| Steve Brunton, "Control Bootcamp" (YouTube) | The inverted pendulum and why it's unstable, with the math, when you want more. |
| Joop Brokking, YABR balancing robot (brokking.net and YouTube) | A complete hobby balancing robot build with code. |
| Åström and Murray, *Feedback Systems* (free online) | The textbook, for later. |

Links were checked by search on 2026-10-21; the MIT PDF couldn't be opened, so search the title if it's gone.
