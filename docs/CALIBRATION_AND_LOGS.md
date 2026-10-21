# Calibration and Reading the Logs

What the gyro calibration does, how we ran it on BB1, and how to read what the firmware prints and saves.

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

### Later logs

From step 2 on, every 200 Hz tick goes into a richer log record (raw sensors, motor commands, PID terms; see [FIRMWARE_PLAN.md](FIRMWARE_PLAN.md) section 6), saved from the web page or `tools/logger.py`. This doc will get a section on those columns when they exist.
