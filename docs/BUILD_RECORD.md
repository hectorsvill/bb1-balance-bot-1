# Build Record (Milestone 1)

Built 2026-10-20 by hectorsvill. What was actually built, the exact STL files printed, and the bring-up test results.

**Directions:** front = the "hectorsvill" side; back = the Pico USB and D153C motor-plug side; left and right seen from behind, facing the front.

## As built

| Area | What's on the robot |
|---|---|
| Chassis | WHEELTEC 2-wheel chassis, 2x MG310P20 7.4 V Hall-encoder gear motors (20:1), original motor bracket, 4x 40 mm brass hex columns |
| Bottom plate | Printed replacement plate (chassis_bottom_plate.stl) |
| Battery | 2S 1300 mAh LiPo, wrapped in tape, lying across the top of the WHEELTEC chassis plate between the brass columns (under the printed frame), held by a printed strap and a velcro strap; yellow XT power plug plus the white balance lead. Motors hang under the plate. Photos 2026-10-21 (IMG_1535 to 1537, not in git) |
| Top frame | Short frame v1.1, 84 tall, wire windows (reverse_t_frame_short.stl) |
| Motor driver | D153C on one face, M2.5 screw carrier, 66.6 pin hole; Dupont sockets on its header pins |
| Controller | Pico W sideways on the other face, M2 screw carrier, 66.6 pin hole (pocket 39 to 63.6 above base), USB toward the back |
| IMU | GY-521 in the base pocket on the Pico side, pad row facing the spine |
| Wiring | See WIRING.md; D153C to Pico wires cross the spine through the wire windows |
| Power | D153C 5V straight to VSYS (no 1N5819 yet) |
| HC-SR04 | Not mounted; the holder goes on the front end (the bottom plate is the same at both ends) |

## STL files printed

All five rebuild exactly from the sources in `cad/` (same volume and size).

| STL | Source and setting | Size (mm) | Notes |
|---|---|---|---|
| reverse_t_frame_short.stl | robot_platform.scad, layout = "short", part = "frame" | 100 x 70 x 84 | 5.4 column sockets, IMU pocket flipped 180, "hectorsvill" on the front flange |
| carrier_d153c_m25.stl | robot_platform.scad, part = "d153c" | 59.6 x 53.6 x 9 | 4x M2.5 screw bosses |
| carrier_pico_sideways_m2.stl | robot_platform.scad, part = "pico_side" | 30.6 x 54.6 x 9 | 4x M2 screw bosses |
| chassis_bottom_plate.stl | chassis_bottom_plate.scad, part = "plate" | 98 x 60 x 3 | PETG, 100% infill |
| hcsr04_holder.stl | chassis_bottom_plate.scad, part = "hcsr04_holder" | 54 x 25 x 28.4 | not mounted yet |

Rebuild example: `openscad -o reverse_t_frame_short.stl -D 'layout="short"' -D 'part="frame"' cad/robot_platform.scad`

## Hardware used

- 4x M2.5 screws: base onto the columns
- 4x M2.5 screws: D153C onto its carrier
- 4x M2 x 6 hex screws: Pico W onto its carrier
- 2x 10 mm pieces of 1.75 mm filament: carrier lock pins
- Battery strap

## Bring-up test results (2026-10-20)

| Test | Result |
|---|---|
| Motor A (socket A) | Left wheel. a- rolls forward, a+ backward |
| Motor B (socket B) | Right wheel. b+ rolls forward, b- backward |
| Encoder 1 (left) | a-: +3463 counts in 2 s (~1,730/s); a+: -3497 |
| Encoder 2 (right) | b+: -3605 (~1,800/s); b-: +3527 |
| Motor match | Within about 3% one at a time at 100/255 |
| f (both forward, 100/255, 2 s) | left +2442, right +2257: signs correct |
| r (both backward) | left -2842, right -3105: signs correct |
| Counts per wheel turn | Not measured cleanly; 520 assumed |
| MPU-6050 | Live readings on I2C0 (WHO_AM_I line not captured) |
| At rest | accel z about 0.94 g (flat, face up), total about 0.95 g |
| Gyro bias | y about +2.5 deg/s at rest |
| Pitch | tilt X; 30 deg forward read about -35 deg, so forward lean reads negative. **Corrected 2026-10-21:** bb1 step 1 shows forward lean (hectorsvill side down) reads positive, gyro X shares the sign |

## Hand-held arm test (2026-10-21, bb1 step 7)

Run with `tools/handheld.py` (serial arm, Wi-Fi log), kp 15, kd 0.5, trim −3.98, cap 80. File: `logs/bb1_2026-10-21_22-17-57_handheld.csv` (22.9 s, 0 missing ticks, 0 overruns, 0 IMU errors).

| Check | Result |
|---|---|
| Arm refused when not near the trim | yes (reason "not upright" in the 22:12 runs) |
| Stayed armed for the full arm time | 5.00 s, then "arm time finished" |
| Lean forward (error > +3 deg) | PWM +66 both, wheel speed +215 / +289 counts/s: **forward, correct** |
| Lean back (error < −3 deg) | PWM −60 both, wheel speed −243 / −237 counts/s: **backward, correct** |
| Fall cut-out (past 45 deg) | **works both ways** (`logs/bb1_2026-10-21_22-21-22_handheld.csv`): forward cut at error +45.5 deg, back at −45.5 deg, PWM 0 the same tick, FALLEN |

## First floor attempts (2026-10-21, kp 15, kd 0.5, trim −3.98, cap 80)

File: `logs/bb1_2026-10-21_22-31-13_floor-kp15.csv`. Three tries, each stood about 2 s, then fell **backward** and the 45 deg cut-out stopped the motors (FALLEN).

- Every try: it hovers near the trim for 1 to 1.5 s, starts leaning back, the PWM grows and **hits the cap (80) at about 5 deg** of lean. From then on the wheels can't go faster (about 340 counts/s) and it falls in about 0.2 s.
- Below PWM about 15 the wheels don't turn at all (speed 0): the motor deadband.
- Reading: cap 80 is the limit, as in PID_TUNING.md ("wheels spin but it still falls"). Next: cap 150, same gains.

## Floor attempts 2 (2026-10-21, file `logs/bb1_2026-10-21_22-41-56_floor-cap150.csv`)

Meant to test cap 150, but the cap **stayed at 80** the whole run (gains_ver 2 in every row, max PWM 80), so this is a second run of kp 15 / kd 0.5 / cap 80. Seven tries:

- **Try 1 stood the full 10 s** (stopped by the arm timer). It rocked about ±4 deg around upright, pushing up to the cap now and then. Average pitch −4.6 deg, slightly behind the trim (−3.98): the real balance point may be about 0.6 deg further back.
- Tries 2 to 7: 0.5 to 3.2 s, five fell backward and one forward. Each fall happened while the PWM sat at the cap (57 to 193 ticks at the cap).
- The robot drifted backward over the run (encoders about −4,000 counts on each side): it only fights the lean, nothing holds its position yet (that's the speed loop, step 8).
- Next: really set cap 150 (check with `g` before arming).

