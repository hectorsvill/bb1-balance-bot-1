# PID for a balancing robot: a short lesson

For BB1, written for an FPV pilot. The full tuning guide is [PID_TUNING.md](PID_TUNING.md); this page is the "why".

## 1. The problem

BB1 is an inverted pendulum: a weight on a stick, balanced on wheels. Upright is **unstable**: any lean grows by itself, faster and faster (gravity pulls harder the more it leans). The only thing that can save it is driving the wheels **under** the lean, like moving your hand under a falling broom.

A quad is different: hovering is close to stable already, PID just makes it snappy. On BB1, **without the controller it falls in about half a second**. That's why it's touchy.

## 2. The loop (200 times a second)

```
error  = pitch - trim                  (deg, + = leaning forward)
P term = P gain x error                (lean -> push)
I term = I gain x (error added up)     (lean that lasts -> push harder)
D term = D gain x pitch rate           (falling fast -> brake)
PWM    = P + I + D, limited to +-cap   (+ = wheels forward)
```

Same as one axis of Betaflight's angle mode, with the stick fixed at "upright" (the trim).

## 3. What each term does on BB1

| Term | Job | Too little | Too much | On a quad |
|---|---|---|---|---|
| **P** | push the wheels under the lean | falls over slowly, wheels too gentle | fast back-and-forth shaking | P wobble |
| **D** | brake the fall, stop the overshoot | overshoots, big swings, bounce-back | buzzing, jitter, motors slam +-cap | hot motors, D noise |
| **I** | fix a lean that never goes away | leans one way all the time | slow, growing rocking; falls late | holds angle in wind / heavy side |
| **Trim** | the angle where it really balances | rolls away one way, falls that way | (same, the other way) | accelerometer trim |
| **Cap** | the most PWM it may use | motors max out ("at cap") and it falls | harder falls, harsher on the driver | motor output limit |

## 4. Do I need I?

**Mostly no, not yet.** I fixes a *steady* lean (a wrong trim, a heavy side). A good trim does the same job with no side effects. On a balancing robot I is risky: it keeps adding up while BB1 rolls and swings, then releases late and makes it rock. Most balancing robots run **P and D only** on the angle, and put I (if any) in the speed loop (step 8).

Your own log agrees (2026-10-22, `bb1_2026-10-22_22-20-29`): with I 0 every try at P 9 to 9.85 / D 0.74 / cap 150 reached the timer; after adding I 2 (and cap 45) most tries fell.

## 5. Why the cap matters

The cap is the strongest push BB1 is allowed. When it falls further than the cap can catch, it's gone no matter how good P and D are. The report's `cap%` column shows it: at cap 150 it was at the cap 0% of the time; at cap 45 it was there 10 to 75% of the time and fell. Keep the cap high enough that `cap%` stays near 0 in a good try.

## 6. The tuning method

1. **I = 0. Trim first**: hold it at its balance point, "Trim = current pitch".
2. **P**: raise until it fights back and stands a few seconds; too much = fast shaking.
3. **D**: raise until the swings calm down; stop before jitter (D term bigger than P, PWM flipping many times a second).
4. Go back and forth: a bit more P, a bit more D, while it gets calmer.
5. **One change at a time**, 3 tries each, read the report, keep the setting with the smallest `wobble` that always reaches the timer.
6. **Save to flash** when you have one.
7. What's left after that (slow drift, rolling away) is **not** a P / I / D problem: it's the missing speed loop (step 8), like angle mode without position hold.

## 7. Reading a log

`tools/venv/bin/python tools/pid_report.py` (newest log) prints one row per try:

| Column | Look for |
|---|---|
| `secs`, `end` | `timer` = it never fell |
| `wobble` | smaller = calmer. Compare settings with this |
| `lean` | far from 0 = trim off (+ forward) |
| `\|P\| \|I\| \|D\|` | P should do most of the work; D under P; I small |
| `cap%` | near 0 in a good try |
| `flips/s` | tens = jitter (D too high) |
| `turns` | more than 2 = drift, step 8's job |

`--plot` draws each try: error on top, P / I / D terms and PWM below. Things to spot: D spikes bigger than P = jitter; PWM flat at the cap just before a fall = cap or P; error drifting one way the whole try = trim.

## 7b. The speed loop (step 8, built 2026-10-24)

PD on the angle has no idea BB1 is rolling. A trim that's 0.2 deg off makes it hold a small lean, and holding a lean means accelerating: it rolls faster and faster until the motors max out. On wood that took about 2 s every time (TUNING_LOG rounds 16 to 20).

The speed loop is an outer loop that moves the angle loop's target:

| Gain | Does | Too little | Too much | On a quad |
|---|---|---|---|---|
| **Speed P (sp)** | rolling forward -> target leans back, so BB1 brakes | still runs away, just slower | rocks back and forth slowly, overcorrects the speed | how hard position hold resists drift |
| **Speed I (si)** | rolled away from start -> lean back toward it; cancels trim error | drifts away slowly, never comes back | big slow back-and-forth around the start point | position hold pulling back to the hold point |

Tune order: angle P / D first (done enough), then sp up until it stops running away, then a little si until it stays in place.

## 8. Words you'll meet

- **Setpoint**: the target angle (our trim).
- **Saturation**: output stuck at the cap.
- **Windup**: I keeps growing while saturated, then overshoots. `pid.h` stops I growing at the cap.
- **Derivative on measurement**: BB1's D uses the gyro rate directly, not the change in error, so it's smooth and doesn't kick when the trim changes.
- **Cascade control**: a slow outer loop (speed) sets the target for a fast inner loop (angle). That's step 8.
