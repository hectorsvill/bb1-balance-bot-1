# PID Tuning Guide for BB1

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

## 4. Tuning steps on BB1

All gains change live from the page. Change one thing at a time and write down what you saw.

1. **Kp only** (Ki = 0, Kd = 0). Start small and raise it until the robot fights back when tilted and starts to oscillate fast around upright. That's roughly "too much".
2. **Add Kd.** Raise it until the oscillation calms down. Too much D makes the motors buzz or jitter (it amplifies noise). Then try raising Kp a bit more, and Kd again.
3. **Add a little Ki** only if it keeps leaning or drifting one way. Small steps.
4. **Speed loop** (later step): raise its gain until the robot stops wandering, but not so much it rocks slowly back and forth.
5. **Save** the gains (disarmed) and log a run with the gains in the note.

Use the **step test** (setpoint +2 deg for 1 s) to compare gains: a good response settles in well under a second with one small overshoot.

| You see | Likely cause | Try |
|---|---|---|
| Falls over slowly one way | Kp too low, or trim wrong | raise Kp, check trim |
| Fast back-and-forth shaking | Kp too high, Kd too low | more Kd or less Kp |
| Buzzing, jittery motors | Kd too high or noisy gyro | lower Kd |
| Slow rocking, big swings | I or speed loop too strong | lower Ki / speed gain |
| Leans steadily, rolls off | balance trim off | adjust trim, small Ki |
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
