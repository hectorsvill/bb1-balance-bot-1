# PID Tuning Guide for BB1

How the balance controller works, how to tune it on the real robot with the web page, and where to read more.

## 1. The idea in one paragraph

A balancing robot is an upside-down pendulum. If it leans forward, the wheels must roll forward to get back under it, the same way you balance a broom on your hand. The controller looks at how far it leans (the error) and how fast it is falling, and turns that into a motor command.

## 2. The three terms

```text
error  = setpoint - pitch          (setpoint is the balance angle, about 0 deg)
P term = Kp * error                 how far am I leaning?   -> push back harder the more I lean
I term = Ki * sum(error * dt)       have I been leaning for a while? -> fix a steady offset
D term = -Kd * pitchRate            how fast am I falling?  -> damp the motion, stop overshoot
pwm    = P + I + D                  (clamped to the PWM cap)
```

- **P** does most of the work. Too little: it falls slowly in one direction. Too much: it shakes back and forth fast.
- **D** is the brake on P. It reacts to speed, not position, so it calms the oscillation. We use the gyro rate directly (it *is* the derivative of the angle), which is much less noisy than subtracting two angles.
- **I** cleans up a steady lean (for example the battery not being centered). Small, and clamped (anti-windup) so it can't build up while the robot lies on the floor.

Later the **speed loop** sits outside this one: if the wheels keep rolling forward, it tilts the setpoint back a fraction of a degree so the robot stops drifting.

## 3. Before tuning

- Pitch angle steady and correct sign (step 1 of the firmware plan).
- Find the balance point: hold the robot where it feels balanced, read the pitch, and set it as the trim.
- Find the motor deadband: the PWM at which the wheels just start to move. The controller adds it so small commands aren't lost.
- PWM cap low (150/255) to start. Something soft to fall on.

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
