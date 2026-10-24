# Power-on and auto-arm

What BB1 does from the moment you switch it on, and every edge case around it.
**Status (2026-10-24):** reworked into `firmware/bb1/auto_arm.h`, flashed, not tested
yet. Picks made with the recommended defaults while Hector was away
(1 s hold, lift detector, 3 falls in 30 s); any of them can change.

## The two main cases

Nothing else is needed: no web page, no script (`handheld.py` is only for
recording test logs) and no Wi-Fi. It stops only when you press STOP (web page
or serial), when it falls or is laid down, or on a safety stop (IMU errors,
lift detector).

1. **Switched on standing upright** on the floor: it measures the gyro bias
   (2 s, must be still), then, once it has been within 3 deg of the trim and
   still for 1 s, it starts balancing. About 3.5 s from switch-on.
2. **Switched on lying down**: it measures the bias and waits. Stand it up and
   hold it still (hands just touching) for 1 s: it starts balancing. Let go.

"Still" = pitch rate under 30 deg/s. Lying down reads about +-89 deg, so it
never gets near the 3 deg window and just waits.

## Edge cases

| # | Situation | Before (first auto-arm) | Now (`auto_arm.h`) |
|---|---|---|---|
| 1 | Switched on upright, still | Balances after cal + 1 s | Same |
| 2 | Switched on lying down | Waits; stand it up + 1 s | Same (`AUTO_ARM_HOLD_MS`, 1 s; Hector asked for 1 to 3 s) |
| 3 | Switched on while held or moving | Bias cal fails, retries every 3 s; can't arm until it passes | Same |
| 4 | Switched on mid-fall or leaning | Waits until within 3 deg and still | Same |
| 5 | Held upright **in the air**, still 1 s | **Arms: wheels spin in your hands** | Arms, then the lift detector stops the wheels after 0.5 s of fast spinning |
| 6 | Picked up **while balancing** | Only the 45 deg cut-out; held level it keeps driving, wheels race toward the cap | Lift detector: motors off, reason "picked up"; set it down and hold still to re-arm |
| 7 | Carried upright and steady | Same as 5 | Same as 5 |
| 8 | Falls (past 45 deg) | Motors off; stand it up + 1 s and it balances again | Same |
| 9 | Falls again and again | Re-arms every time, forever | 3 falls (or lifts) within 30 s: auto-arm off until ARM (`off: fell too often`) |
| 10 | STOP (page, space bar, Enter, serial `disarm`) | Auto-arm off until ARM or power-up, even if pressed while lying down | Same: STOP always wins |
| 11 | ARM pressed | Arms now (within 10 deg), turns auto-arm back on | Same, and forgets old falls. Even a refused ARM turns auto-arm back on |
| 12 | Arm timer runs out, page lost, page hidden (after a page ARM) | Auto-arm off until ARM | Same |
| 13 | IMU errors, loop running late | Disarm; auto-arm off | Same (something is broken) |
| 14 | `cal` while balancing | Disarms; auto-arm off | Same |
| 15 | Battery low (under 3.3 V per cell) | Won't auto-arm. Already balancing: keeps going | Same; the page shows it red |
| 16 | No battery reading (wire off, switch off) | Auto-arm allowed | Same |
| 17 | No Wi-Fi at all | Auto-arm works: no page, no timer, no heartbeat needed | Same. A fall, a lift, IMU errors or STOP over serial stop it |
| 18 | Motor test running | No auto-arm | Same |
| 19 | `auto 0` | Only ARM starts it; saved in flash | Same |
| 20 | Trim changed while waiting | The 3 deg window follows the new trim | Same |
| 21 | Flashing over USB | Reboots; if upright on the desk it arms | Rule: **lay it down before flashing** |

### Lift detector (cases 5, 6, 7)

On the floor a balancing BB1 rolls slowly: the 2026-10-24 logs never went past
2 wheel turns/s, even in runs that ran away. In the air the wheels have nothing
to push against, so they spin up fast (about 3.4 turns/s at PWM 100) while the
body stays upright and the 45 deg check never fires. Rule (`control.h`, any
arm, not just auto-arm): both wheels averaging over `LIFT_TURNS_PER_SEC` (4)
for `LIFT_MS` (0.5 s) = lifted, motors off, reason "picked up". It counts like a
fall: auto-arm waits to be stood up again. So when you pick it up while it's
balancing, expect about half a second of spinning wheels before it stops.
Laying it down (past 45 deg) still stops it at once.

## The code

Before, auto-arm was spread over `balanceDisarm()`, `balanceArm()` and
`balanceAutoArm()` in `control.h`, with one hard-to-read line deciding when it
turned off. Now it is one small file, `auto_arm.h`, with named states you can
read top to bottom:

```text
OFF_SETTING      auto 0
OFF_STOPPED      STOP (or another non-fall disarm); waits for ARM
WAIT_CAL         gyro bias not measured yet
WAIT_UPRIGHT     lying down, leaning, moving, or battery low
HOLDING          upright and still, counting to the hold time
BALANCING        armed
```

`autoArmUpdate()` picks one state each tick (core 1, before the balance
loop) and returns true when it's time to arm. `autoArmStop()` (STOP) and
`autoArmOnManualArm()` (ARM) are called where those requests are handled in
`bb1.ino`. The state is printed on serial when it changes (`auto-arm: waiting:
stand it up and hold still`), shown on both pages next to the mode, sent in
`/state` (`auto_state`) and logged every tick (`auto_state` column, log
format 7), so you can always see why it is or isn't arming.

## Settings (`config.h`)

| Name | Value | Meaning |
|---|---|---|
| `DEFAULT_AUTO_ARM` | 1 | on at power-up (setting `auto`, saved in flash) |
| `AUTO_ARM_ANGLE` | 3 deg | how close to the trim counts as upright |
| `AUTO_ARM_RATE` | 30 deg/s | how slow counts as still |
| `AUTO_ARM_HOLD_MS` | 1000 | how long upright and still before it arms |
| `AUTO_MAX_FALLS` / `AUTO_FALL_WINDOW_MS` | 3 / 30 s | too many falls: off until ARM |
| `LIFT_TURNS_PER_SEC` / `LIFT_MS` | 4 / 0.5 s | lift detector |

