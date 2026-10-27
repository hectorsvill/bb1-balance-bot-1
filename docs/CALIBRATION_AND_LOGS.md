# Calibration and Reading the Logs

What the gyro calibration does, how we ran it on BB1, and how to read what the firmware prints and saves. Section 4 covers the full log records (step 2 on).

## 1. What calibration does

### The problem: gyro bias

The MPU-6050's gyro measures how fast the robot rotates (deg/s). Sitting perfectly still it should read 0, but every MEMS gyro reads a small constant offset instead, called **bias**. Ours: about +2 deg/s on Y, -2 on Z, -0.5 on X.

That sounds tiny, but the angle comes from adding up the rate every tick. A bias of 0.5 deg/s left in makes the gyro think the robot tilted **30 degrees in one minute** while it sat still. The complementary filter's accelerometer part pulls it back, but the angle would sit a little off and the balance controller's D term (which uses the rate directly) would push the motors for no reason.

### The fix

Measure the bias while the robot is still, then subtract it from every reading.

`imuCalibrate()` in [firmware/bb1/imu.h](../firmware/bb1/imu.h):

1. Sets the bias to 0 so it reads the gyro raw.
2. Takes 400 readings, 5 ms apart (2 s), and adds them up per axis.
3. Tracks the lowest and highest gyro X reading. If they differ by more than 4 deg/s (`CAL_MAX_SPREAD` in `config.h`), the robot moved: it keeps the old bias and prints "robot moved".
4. Otherwise bias = sum / count (the average), for X, Y and Z.

After that, `imuRead()` subtracts the bias from every reading, so a still robot reads about 0.0 deg/s.

The filter is also restarted from the accelerometer angle after `cal`, so pitch jumps straight to the right value.

### When it runs

- **At power-up**, 0.5 s after boot. Keep your hands off for the first 2.5 s.
- **Any time you type `cal`** in the serial monitor (later: a button on the web page).

Bias changes a little with temperature, so recalibrate if the robot has warmed up or the rate doesn't read about 0 at rest.

### How to calibrate

1. Lay the robot down so it can't move: on its front (hectorsvill side facing the floor) works well.
2. Hands off. Type `cal` and press Enter.
3. It prints the bias, for example `bias deg/s  x -0.50  y 2.07  z -2.20`.
4. Check: `rate` now reads 0.0 (give or take 0.1) while it lies still.

### What we got (2026-10-21)

| Run | Bias x / y / z (deg/s) | Rate at rest after |
|---|---|---|
| Boot, while handled | (not printed) | about +0.5: bad, robot moved during calibration |
| `cal` lying on its front | -0.50 / +2.07 / -2.20 | 0.0 for 20 s, pitch held exactly +88.7 |

Lesson: the boot calibration only works if nobody touches the robot. That's why `cal` exists.

### Not calibrated (yet)

- **Accelerometer offsets**: not needed for balancing. A small accel error just shifts where "upright" reads, and the balance trim (measured before the PID step) absorbs that.
- **Balance point / trim**: the pitch where BB1 actually balances. Measured later, by hand, before tuning.

## 2. Reading the serial output

The normal readout, 10 lines per second:

```text
pitch  +88.7 | accel only  +88.7 | rate    -0.0 deg/s | loop avg  450 us max  525 us | overruns 0 imu errors 0
```

| Field | Meaning | Healthy |
|---|---|---|
| `pitch` | The filtered angle, deg. Forward lean (hectorsvill side down) is positive. This is what the balance controller will use. | follows the tilt, no drift at rest |
| `accel only` | Angle from gravity alone, for comparison. Jumps when you tap or push the robot. | close to `pitch` when still |
| `rate` | Gyro pitch rate, deg/s, bias removed. Positive while tilting forward. | 0.0 at rest |
| `loop avg` / `max` | Time one control tick spent working, in microseconds. Budget is 5000 (200 Hz). | well under 5000 (we're at about 450) |
| `overruns` | Ticks where the work took longer than 5 ms. | 0 |
| `imu errors` | Failed I2C reads. | 0 |

## 3. Capturing and reading a log

### Capture

`csv` switches the output to comma-separated lines (type `csv` again to switch back; it also resets when the Pico restarts):

```text
ms,pitch,accel_pitch,pitch_rate,loop_avg_us,loop_max_us
351872,6.00,6.09,0.41,446,458
```

`ms` is milliseconds since the Pico booted. To save 60 s of it to a file:

```bash
tools/venv/bin/python tools/serial_log.py -t 60 -s csv -o logs/my_run.txt
```

(`-s csv` sends the `csv` command first). Files in `logs/` stay out of git. Run these from the repo folder; [TOOLS.md](TOOLS.md) explains folders and the venv.

### Summarize

```bash
tools/venv/bin/python tools/summarize_serial.py logs/my_run.txt
```

prints one line per second (averages), then the totals:

```text
second  pitch  accel_pitch  rate  loop_max_us
0        88.7         88.8   0.0          459
...
21       88.5         88.3  -4.1          500
22       80.2         80.0  -3.8          493
23       42.3         41.1 -59.9          514     <- standing it up: rate big and negative
24        2.8          3.1 -24.5          458
26       -2.9         -2.9  -0.2          504     <- upright, still
```

How to read it:

- **Still periods**: `rate` near 0, and `pitch` equal to `accel_pitch` within a few tenths. If `pitch` creeps away while `rate` isn't 0, recalibrate.
- **Moves**: `rate` shows how fast and in which direction (negative = tilting back). Here, `pitch` going down with `rate` negative means the signs agree.
- **`loop_max_us`** should stay far below 5000.
- The "largest gap" line at the end includes slow moves, so a few degrees is normal there.

Add `--plot` to see pitch, accel-only and rate over time in a chart.

### Our runs

| File (in `logs/`) | What | Result |
|---|---|---|
| `2026-10-21_step1_tilt.txt` | upright, forward, back, signs -1 | forward read negative, so signs flipped to +1 |
| `2026-10-21_step1_tilt2.txt` | front, upright, forward, signs +1 | pass |

The vault note `Logs/2026-10-21 bb1 step1 - tilt test` has the write-up.

## 4. The log buffer and dumps (step 2 on)

Since step 2, core 1 writes one **log record** every 5 ms tick into a ring buffer that always holds the newest 10 s (`LOG_SECONDS` in `config.h`). The readout above is computed from those same records, so what you see is what gets logged.

`dump` prints the buffer as CSV (about the last 9 s: it skips the oldest second so core 1 can't overwrite records mid-print). `dump 3` prints only the last 3 s. Save and check one:

```bash
tools/venv/bin/python tools/serial_log.py -t 5 -s dump -o logs/my_dump.txt
```

```bash
tools/venv/bin/python tools/check_dump.py logs/my_dump.txt --plot
```

`check_dump.py` reports missing ticks (should be 0), tick spacing (should be exactly 5000 us), work time, errors and the pitch range.

A dump file looks like this:

```text
# bb1 log dump, step 2 firmware
# gyro_bias_dps,-0.418,2.099,-2.149
# alpha,0.980,loop_hz,200,accel_lsb_per_g,16384.0,gyro_lsb_per_dps,65.5
tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,pitch_rate,accel_pitch
2225,13612345,451,2,0,16380,-210,402,-1520,-27,137,-141,88.452,0.021,88.531
# end, 1800 records
```

Lines starting with `#` are settings and notes, so the file explains itself later (and a model trained on it knows the bias and scales).

### Other ways to save a log (step 4 on)

Every way of saving writes the same layout, so `check_dump.py` and `load_dump()` read all of them:

| Way | Good for |
|---|---|
| serial `dump` | the robot is on USB anyway |
| web page **Record / Download**, **Save last 10 s** | everyday runs, from a phone or the Mac (WEB_INTERFACE.md) |
| `tools/logger.py` | long runs, building a training set (TOOLS.md) |

Since step 4 the top lines come from `log_meta.h`:

```text
# firmware,bb1 step 4
# log_format,1
# gyro_bias_dps,-0.418,2.099,-2.149
# alpha,0.980,loop_hz,200,accel_lsb_per_g,16384.0,gyro_lsb_per_dps,65.5
# accel_pitch_sign,1,gyro_pitch_sign,1
# note,tilt test
# recorded_at,2026-10-22T10:15:00-04:00
# records,2400,lost,0
```

`logger.py` and `handheld.py` add `# gains_vN,...` (one per gains version used), `# surface,...`, `# note,...`, `# recorded_at,...` and `# records,...`. `drive_test.py` also adds one `# step,name,speed,turn,from_tick,to_tick` line per step (what was asked, and when) and `# drive_test,<test>,<how it ended>`; `dataset.py` turns the step lines into a `step` column.

`log_format` goes up whenever the columns change. Files whose firmware says `MOCK` came from the offline mock: made-up data, keep them out of any analysis or training set.

### Columns

| Column | Unit | Meaning |
|---|---|---|
| `tick` | count | tick number since boot; goes up by exactly 1. A jump means records were lost. |
| `t_us` | microseconds | when the tick started (since boot). Consecutive ticks are 5000 apart. |
| `work_us` | microseconds | how long the tick's work took (budget 5000) |
| `mode` | code | 0 boot, 1 calibrating, 2 disarmed (motors off), 3 motor test running, 4 armed (balancing), 5 fallen (motors off until you arm or disarm again) |
| `flag_bits` | bits | 1 = IMU read failed this tick, 2 = the previous tick overran 5 ms, 4 = no successful gyro calibration yet (the robot was moving at startup; `pitch_rate` still includes the bias, and BB1 retries every 3 s), 8 = armed and the PID output hit the PWM cap (saturated), 16 = a drive command ran out or was stopped and BB1 is ramping to a stop (log format 9), 32 = encoder fault: one wheel read near 0 while the other spun for 0.3 s, heading hold off until the next arm (2026-10-25), 64 = the angle I term is being drained after a drive (`irelax`, 2026-10-26; same log format, the bit was unused before). Usually 0. |
| `ax_raw` `ay_raw` `az_raw` | counts | accelerometer exactly as the chip sent it. g = raw / 16384 |
| `temp_raw` | counts | chip temperature. deg C = raw / 340 + 36.53 |
| `gx_raw` `gy_raw` `gz_raw` | counts | gyro exactly as sent, **bias not removed**. deg/s = raw / 65.5 - bias |
| `pitch` | deg | filtered pitch, forward + |
| `pitch_rate` | deg/s | gyro X, bias removed, forward + |
| `accel_pitch` | deg | accelerometer-only pitch |
| `enc_l` `enc_r` | counts | encoder counts since boot, forward +. 520 per wheel turn (assumed) |
| `speed_l` `speed_r` | counts/s | wheel speed over the last 10 ticks (50 ms), forward +. **Logs before format 10 (before 2026-10-26) read 3.3x low**: the `constrain()` macro called the speed update 3 times a tick, so they are counts over the last 3 ticks x 20. Multiply by 10/3 for counts/s (`dataset.py` does), or use the `enc_l`/`enc_r` differences |
| `pwm_l` `pwm_r` | -255..255 | what was sent to each motor this tick, forward +. 0 = motor off. This is the robot's "action" for training. |
| `setpoint` | deg | the balance trim in use (step 6) |
| `p_term` `i_term` `d_term` | PWM | the PID's three parts this tick (0 when disarmed). pwm = p + i + d (+ deadband), clamped to the cap |
| `gains_ver` | count | which gains were in use; the `# gains_ver,...,kp,...` line at the top gives the values at the time of saving; `handheld.py` files also have a `# gains_vN,...` line for every version used during the run. Goes up on every change. |
| `reason` | code | why it last disarmed or refused to arm: 0 none, 1 you, 2 fell, 3 IMU, 4 lost the page (heartbeat), 5 loop late, 6 not upright, 7 not calibrated, 8 motor test running, 9 calibrating, 10 page hidden, 11 arm time finished, 12 picked up, 13 battery low (under 6.6 V with a sense reading; log format 4, codes 12 and 13 added later) |
| `web_age_ms` | ms | time since the page last asked for `/state` (10 ms steps, 2550 = 2.55 s or more). Normally 0 to about 100. Big values show Wi-Fi stalls; over 1000 while armed from the page = heartbeat disarm (log format 5) |
| `batt_v` | V | battery voltage, **whole pack** (2S LiPo; the pages show it per cell), from the D153C's `ADC` pin (battery / 11) on GP26, read on core 0 every 100 ms, 16 samples averaged and smoothed (`battery.h`). 0 = no wire or the D153C switch off. Sags while the motors push. (log format 6, 2026-10-24) |
| `auto_state` | - | what auto-arm is doing (`auto_arm.h`): 0 off (auto 0), 1 off (stopped, waits for ARM), 2 off (fell too often), 3 no IMU (log format 7: waiting for calibration), 4 waiting for upright, 5 holding (counting to 1 s), 6 balancing. (log format 7, 2026-10-24; format 8 changed what 3 means) |
| `drive_speed` | wheel turns/s | the **operator's drive command** in force this tick (`POST /drive`, the Drive pad, serial `drive`): forward +, 0 when none or once it ran out. Steps of 0.01. What BB1 actually aims for ramps toward this (`# drive_accel` in the header). This is the action a model would learn to copy (log format 9, 2026-10-25) |
| `drive_turn` | turns/s | the operator's turn command: left-minus-right wheel difference, right turn +. Same rules as `drive_speed` (log format 9) |
| `drive_speed_now` | turns/s | the ramped speed target BB1 really aims for this tick (follows `drive_speed` at `# drive_accel`). The controller's own state, for training (log format 11, 2026-10-26) |
| `drive_turn_now` | turns/s | the ramped turn target (log format 11) |
| `speed_pwm` | PWM | what the wheel speed loop added to both wheels this tick (`smode 1`; always 0 in `smode 0`). Not in `p_term`/`i_term`/`d_term`; `pwm_l`/`pwm_r` include it (log format 11) |

Raw values are kept on purpose: a better filter, or a model, can be re-run on them later. Step 5 added the six wheel columns (log format 2), step 6 the five controller columns (log format 3); later steps add the speed loop and drive commands at the end.

In your own Python, `load_dump()` from `tools/check_dump.py` gives you a pandas table:

```python
import sys; sys.path.insert(0, "tools")
from check_dump import load_dump
df, settings = load_dump("logs/my_dump.txt")
df.pitch.describe()
```
