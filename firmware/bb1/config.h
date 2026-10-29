// config.h - every number you might want to change, in one place.
//
// Pins and signs match CLAUDE.md and docs/WIRING.md. If you rewire something or
// a sign turns out wrong, this is the only file to edit.

#pragma once

// ---------- pins (Pico W GPIO numbers, not physical pin numbers) ----------
const int PIN_IMU_SDA = 4;     // I2C0 SDA, Pico pin 6
const int PIN_IMU_SCL = 5;     // I2C0 SCL, Pico pin 7

const int PIN_STBY = 9;        // D153C standby: HIGH = motors can run, LOW = both off
const int PIN_PWMA = 10;       // left motor speed
const int PIN_PWMB = 11;       // right motor speed
const int PIN_AIN1 = 12;       // left motor direction pair
const int PIN_AIN2 = 13;
const int PIN_BIN1 = 14;       // right motor direction pair
const int PIN_BIN2 = 15;
const int PIN_E1A  = 16;       // left encoder
const int PIN_E1B  = 17;
const int PIN_E2A  = 18;       // right encoder
const int PIN_E2B  = 19;

// ---------- directions (front = the "hectorsvill" side) ----------
// Motors and encoders: from the f / r test (2026-10-20). Not used until step 5.
const bool LEFT_MOTOR_REVERSED  = true;
const bool RIGHT_MOTOR_REVERSED = false;
const bool LEFT_ENC_REVERSED    = false;
const bool RIGHT_ENC_REVERSED   = true;

// IMU: the chip's X-axis tilt, atan2(ay, az), reads POSITIVE when the robot
// leans forward (measured 2026-10-21: hectorsvill side down read +26 deg once
// these were set right), so no flip is needed: +1.
// The accel angle and the gyro rate must always share a sign. If you ever flip
// one, flip both. Check: while you tilt forward, "rate" is positive and
// "pitch" and "accel only" move together.
const float ACCEL_PITCH_SIGN = +1.0;
const float GYRO_PITCH_SIGN  = +1.0;

// ---------- timing ----------
const int LOOP_HZ = 200;                          // control loop rate
const unsigned long LOOP_US = 1000000UL / LOOP_HZ; // 5000 us = 5 ms per tick
const int PRINT_HZ = 10;                          // how often the serial readout updates

// ---------- logging ----------
// The ring buffer keeps the newest LOG_SECONDS of ticks in RAM (see telemetry.h).
// 10 s x 200 Hz x 40 bytes = 80 KB. If Wi-Fi later needs the RAM, lower this.
const int LOG_SECONDS = 10;
const int LOG_BUFFER_RECORDS = LOG_SECONDS * LOOP_HZ;

// ---------- battery (2026-10-24) ----------
// D153C "ADC" pin (battery / 11 on the board) wired to GP26. battery.h reads it.
const int   BATTERY_PIN         = 26;     // ADC0, Pico pin 31
// D153C R13 10 k / R15 1 k = 11 on paper. Calibrated 2026-10-24 against a meter:
// meter 7.9 V, BB1 read 7.2 V with 11, so 11 x 7.9 / 7.2 = 12.07. (The Pico's
// ADC reads a little low, typical for the RP2040.) Re-check with a meter now and then.
const float BATTERY_DIVIDER     = 12.07;
const int   BATTERY_CELLS       = 2;      // 2S LiPo: the pages show volts per cell
const unsigned long BATTERY_READ_MS = 100;
const float BATTERY_WARN_V      = 7.0;    // whole pack: 3.5 V per cell. The page turns orange.
const float BATTERY_LOW_V       = 6.6;    // whole pack: 3.3 V per cell. Page turns red, and (when a reading is present) BB1 disarms / refuses to arm.
const float BATTERY_MIN_SENSE_V = 3.0;    // below this: no wire, or the D153C switch is off

// ---------- motors (step 5) ----------
const int PWM_FREQ = 10000;          // 10 kHz since 2026-10-24 (vendor examples use it): wheels start at lower PWM than at 20 kHz (BUILD_RECORD). May whine a little
const int PWM_MAX  = 255;            // analogWriteRange: 0 = off, 255 = full battery voltage
// Motor test limits. The test always stops by itself after the time you ask
// for, and never runs longer than MOTOR_TEST_MAX_MS or harder than MOTOR_TEST_MAX_PWM.
const int MOTOR_TEST_MAX_PWM = 150;
const int MOTOR_TEST_MAX_MS  = 3000;
// Encoder counts per wheel turn: 13 pulses x 20:1 gearbox x 2 edges (assumed,
// not measured cleanly yet; check by turning a wheel once by hand).
const long COUNTS_PER_REV = 520;
// Wheel speed = counts moved over the last SPEED_WINDOW ticks. 10 ticks = 50 ms:
// at 1750 counts/s a single tick only sees about 9 counts, too coarse on its own.
const int SPEED_WINDOW = 10;

// ---------- balancing (step 6) ----------
// Starting values: the first settings that balanced on wood (2026-10-24, TUNING_LOG round 27).
// Change them live with serial (kp, ki, kd, trim, cap, deadband)
// or the page's Balance panel. "save" stores them in flash (settings.h); saved
// values replace these at power-up, so after changing these type "defaults" then
// "save" (docs/PID_TUNING.md, "Two ways to change the gains").
// P, I, D gains like Betaflight's (pid.h explains each). Units: PWM per deg (P),
// PWM per deg*s (I), PWM per deg/s (D). Serial / page / logs call them kp, ki, kd.
const float DEFAULT_P_GAIN = 18.0;     // a guess to start from; tune per docs/PID_TUNING.md
const float DEFAULT_I_GAIN = 100.0;
const float DEFAULT_D_GAIN = 0.5;
const float DEFAULT_TRIM = 0.0;  // deg: the pitch where BB1 really balances (measured 2026-10-21; page: "Use current pitch")
const float DEFAULT_PWM_CAP = 200;  // PWM limit while armed (of 255). Gentle for first tests; raise once it balances.
const float DEFAULT_DEADBAND = 10;  // PWM added so small corrections still turn the wheels (measured: wheels start at 10-15 in the air, about 30 on wood)
// Speed loop (step 8): leans BB1 back when it rolls forward, so it brakes
// instead of running away. Units: degrees of target lean per wheel turn per
// second (speed P) and per wheel turn rolled since arming (speed I = position).
// 0 = off, which is the angle loop alone, as before step 8.
const float DEFAULT_SPEED_P = 3.0;   // 1.2 rolled on 0.8 turns and rocked after a reverse stop; 3.0 stops in under 0.25 (drive tests, 2026-10-26 evening). Was 4.0 before the constrain() fix
const float DEFAULT_SPEED_I = 0.5;
const float SPEED_SHIFT_MAX = 6.0;   // deg: the speed loop never moves the target more than this
const float SPEED_FILTER    = 0.05;  // per tick: smooths the wheel speed (about 0.1 s) so encoder steps don't kick the angle loop
// Heading hold (2026-10-24): keeps both wheels rolling the same distance, so BB1
// goes straight (the right wheel is about 7% faster at the same PWM). Unit: PWM
// moved from one wheel to the other per wheel turn of difference. 0 = off.
const float DEFAULT_TURN_HOLD = 60;   // 20 turned 0.1-0.4 of turn 0.5 asked; 60 turns 0.46-0.56 both ways, standing unchanged (turn tests, 2026-10-26)
const float TURN_HOLD_MAX_PWM = 40;  // the steering correction never exceeds this
// Driving (milestone 2, drive.h): the page or the API asks for a wheel speed
// and a turn rate; drive.h moves the speed loop's and the heading hold's
// targets, and BB1 leans into it by itself. Like flying a quad in angle mode:
// you ask for a speed, the loops work out the lean. Needs sp/si and th on.
// Units are wheel turns per second (520 counts = 1 turn) until the wheel size is measured.
constexpr float DRIVE_MAX_SPEED = 1.0;   // turns/s, forward or back. On the floor BB1 stays under about 2; the lift detector trips at 4
constexpr float DRIVE_MAX_TURN  = 1.0;   // turns/s of left-minus-right wheel difference (+ = turn right)
// Drive acceleration (setting "dacc", live on /tune, saved in flash, 2026-10-28;
// was the constant DRIVE_ACCEL): turns/s per second the speed target ramps at.
// 1.0 = 0 to full in 1 s, as before. Faster ramps need more lean (aff, lmax below).
const float DEFAULT_DRIVE_ACCEL = 3.0;   // 2026-10-29 floor test (wood, aff 1): 90% of 0.5 turns/s in 0.27 s (was 0.93), stops 0.06-0.11 turns; 1.0 before
const float DRIVE_ACCEL_MAX     = 6.0;
const float DRIVE_TURN_ACCEL  = 2.0;   // the same for the turn target
// Drive feedforward (setting "dff", live on /tune, saved in flash): PWM per
// turn/s of speed target, added straight to both wheels. These motors are
// speed knobs: holding 1 turn/s takes about 105 PWM (first drive log,
// 2026-10-25). Without it the angle loop's I term has to wind up to that
// PWM, and when you reverse it has to unwind all of it first: meanwhile the
// wheels keep rolling the old way, BB1 tips back and falls. Betaflight's
// feedforward does the same job for the sticks.
const float DEFAULT_DRIVE_FF  = 25;   // was 60 (and "105 PWM per turn/s" above), from the 3.3x-low speed. Real slope on wood: about 26 PWM per real turn/s
const float DRIVE_FF_MAX      = 150;
// Drive speed I (setting "sdi", live on /tune, saved in flash): while driving,
// degrees of lean added per second for every turn/s BB1 is slower than asked
// (an I term on the speed loop). On wood the angle loop's I term is the real
// wheel-speed command and the speed loop only moves the setpoint, so with P
// alone BB1 stayed stuck going forward and overshot in reverse (2026-10-26,
// after the speed fix). Quad picture: GPS position hold building tilt until
// you really move. Capped by SPEED_SHIFT_MAX; fades out after a stop. 0 = off.
const float DEFAULT_DRIVE_SPEED_I = 4.0;   // 2 fell a third short at 0.3 turns/s, 6 overshoots (3 runs each, 2026-10-26)
const float DRIVE_SPEED_I_MAX     = 20;
const float DRIVE_LEAN_FADE_S     = 0.5;   // after stopping, the built-up lean fades to 0 over about this long

// Fast drive, step 1 (2026-10-28). Driving at 0.5 turns/s BB1 used about 24 of
// 200 PWM and leaned under 1 deg: the motors have lots left, the firmware's
// lean limits were holding it back. A balancing robot can only speed up by
// leaning first: to accelerate at a it must lean atan(a / g). One turn/s per s
// = 0.178 m/s^2 (177.8 mm per wheel turn) = atan(0.178 / 9.81) = about 1.04 deg.
//
// Acceleration feedforward (setting "aff", live, saved): deg of extra lean per
// turn/s^2 of the ramped speed target's acceleration. While the target ramps
// up forward, lean forward by aff x dacc straight away; ramping down, lean
// back. Physics says about 1.0. Like Betaflight feedforward: act on the stick
// moving, before the speed loop sees any error. 0 = off (as before).
const float DEFAULT_ACCEL_FF  = 1.0;   // the physics value; 2026-10-29: stops 0.14-0.16 turns (were 0.23-0.44) at dacc 1. 0 before
const float ACCEL_FF_MAX      = 5;
const float ACCEL_FF_SMOOTH_S = 0.05;  // s: smooths the feedforward's on/off edges at each end of a ramp (like Betaflight's ff smoothing)
// Lean limit while driving (setting "lmax", live, saved): the speed loop's
// whole setpoint shift (sp + si + sdi + aff) is clamped to this while a speed
// target is set or ramping down. Standing still the clamp is SPEED_SHIFT_MAX
// (6). 6 = as before. Can't go under 6: then starting to drive could cut the
// shift short in one tick (a setpoint step).
const float DEFAULT_LEAN_MAX  = 8.0;   // 2026-10-29: at 1.0 turns/s with dacc 3 the lean sat on 6; with 8 it peaked at 7.5. 6 before
const float LEAN_MAX_MAX      = 15.0;
const float LEAN_LIMIT_EASE_DPS = 10.0; // deg/s: after a drive the clamp eases back down to SPEED_SHIFT_MAX this fast (drive.h says why)

// Trim learn (setting "tl", live, saved; firmware 3.5, 2026-10-29). BB1's real
// balance point sits about +1.3 deg forward of trim (wood, 3.4 logs). The
// speed loop's position term (si) should cancel that, but drive.h moves its
// hold point to the wheels at every stop, so the correction is thrown away
// and BB1 creeps backward again after each command. Trim learn is a second
// position I term that is never reset (only at boot): while standing still,
// every wheel turn rolled backward moves the lean target tl deg forward.
// Unit: deg of lean per wheel turn rolled. 0 = off (and the learned value is
// cleared): the 3.4 behavior.
const float DEFAULT_TRIM_LEARN  = 1.0;
const float TRIM_LEARN_RATE_MAX = 5.0;
const float TRIM_LEARN_MAX      = 3.0;   // deg: the learned correction never goes past this either way

// Speed loop mode (setting "smode", 2026-10-26).
//   0 = the speed loop moves the angle setpoint (sp, si, sdi), as since step 8.
//   1 = wheel speed loop: speed and position errors go straight to the PWM
//       (wsp, wsi), the classic balance-car layout. Why: on wood the angle
//       loop's I term holds the wheel speed, and moving the setpoint never
//       changes it (the angle loop tracks the setpoint, error about 0), so BB1
//       got stuck rolling backward with no command (explore run, 2026-10-26).
//       Meant to be used with a lower ki: the speed loop takes over the "hold
//       the wheel speed" job the angle I term was doing.
// Sign, both modes: too fast forward -> wheels pushed further forward -> they
// run out ahead of the body, it tips back, and the angle loop brakes.
const float DEFAULT_SPEED_MODE     = 0;
const float DEFAULT_WHEEL_SPEED_P  = 20;    // PWM per turn/s faster than the target (= kp 18 x sp 1.2, today's strength)
const float DEFAULT_WHEEL_SPEED_I  = 10;    // PWM per wheel turn ahead of the target position (= kp 18 x si 0.5)
const float WHEEL_SPEED_GAIN_MAX   = 200;
const float WHEEL_SPEED_PWM_MAX    = 100;   // the wheel speed loop never adds more than this (fits the log's int8)
const float DRIVE_POS_MAX_LAG      = 0.3;   // smode 1: the target position runs at the asked speed, at most this many wheel turns from the wheels

// I-term relax after a drive (setting "irelax", 2026-10-26). When a drive
// command has ramped down to a stop, the angle loop's I term decays toward 0
// with this time constant, for 2 x irelax (to about 14%). Why: on wood the
// I term is the real wheel-speed command. After driving it kept -4 to -9 PWM,
// so BB1 rolled backward 0.1-0.4 turns/s with no command, leaning forward,
// and the speed loop couldn't reach it (it only moves the setpoint, which
// the angle loop tracks with error about 0). It rolled out of the box in
// unattended runs. Quad picture: Betaflight's I-term relax, which stops I
// from carrying a move on after you centre the stick. 0 = off.
const float DEFAULT_I_RELAX_S = 0.5;
const float I_RELAX_MAX_S     = 5.0;
const float DRIVE_TURN_FF     = 15;    // PWM per turn/s of turn asked for, added straight to the steering (feedforward, like Betaflight's).
                                      // Tried 45 on 2026-10-26 from a misread log (logged wheel speed is 3.3x low): real turn rate went
                                      // 0.5 asked -> 0.80. At 15 it turned 0.42-0.51 (encoder counts)
const float DRIVE_MAX_LAG     = 0.25;  // wheel turns (about 18 deg of heading): the heading target never runs further ahead of the wheels than this. Was 1.0: BB1 swung 35-90 deg after a turn catching up (floor test 1)
const unsigned long DRIVE_HOLD_MS     = 500;    // a drive command lasts this long unless sent again (the page re-sends every 100 ms)
const unsigned long DRIVE_HOLD_MAX_MS = 3000;   // longest a single command may ask for (ms=...)
// Auto-arm (auto_arm.h, docs/POWER_ON.md): after power-up or a fall, BB1 starts
// balancing by itself once it's stood upright and held still. STOP turns it off
// until the next ARM or power-up, so STOP always means stop.
const float DEFAULT_AUTO_ARM   = 1;     // 1 = on, 0 = off (setting "auto")
const float AUTO_ARM_ANGLE     = 3.0;   // deg: within this of the trim...
const float AUTO_ARM_RATE      = 30.0;  // deg/s: ...turning slower than this...
const unsigned long AUTO_ARM_HOLD_MS = 1000;   // ...for this long, then it arms (no arm timer)
const int   AUTO_MAX_FALLS     = 3;     // this many falls...
const unsigned long AUTO_FALL_WINDOW_MS = 30000;  // ...within this long: auto-arm off until ARM
// Encoder fault detector (control.h, 2026-10-25): one wheel reads near 0 while
// the other spins, for this long, and no turn is asked for = that encoder is
// not counting (a loose wire: the left one stopped counting backward today).
// Bad counts make the heading hold spin BB1 in circles and the speed loop
// under-brake, so while faulted the hold is off, driving is refused, and the
// page and log say so. Cleared by the next arm.
const float ENC_FAULT_MIN_SPEED = 500;   // counts/s the good wheel must be doing (was 150 on the 3.3x-low speed: same real speed)
const float ENC_FAULT_RATIO     = 0.2;   // the other wheel reads under this fraction of it
const unsigned long ENC_FAULT_MS = 300;
// Lift detector (control.h): wheels faster than this for this long while armed
// = picked up or in the air, motors off. On the floor it stays under about 2.
const float LIFT_TURNS_PER_SEC = 4.0;   // wheel turns per second, average of both wheels. Until 2026-10-26 the speed read 3.3x low,
                                        // so this really needed about 13 turns/s and never tripped (in the air: about 3.4 at PWM 100)
const unsigned long LIFT_MS    = 500;
// Limits for the settings above, wherever they're set from (serial, the pages, flash).
const float GAIN_P_MAX = 100;
const float GAIN_I_MAX = 100;
const float GAIN_D_MAX = 20;
const float TRIM_MAX = 20;         // deg either way
const float DEADBAND_MAX = 100;
const float SPEED_GAIN_MAX = 20;
const float TURN_HOLD_GAIN_MAX = 200;
const float ARM_SECONDS_MAX = 600; // longest arm timer; 0 = no timer
const float ARM_ANGLE   = 10.0;    // deg: can only arm within this of the trim
const float FALL_ANGLE  = 45.0;    // deg: past this while armed = fallen, motors off
// Armed over Wi-Fi (a page or handheld.py --wifi): disarm if nobody asks for /state
// this long. 2 s since 2026-10-23: home Wi-Fi has about 1 s gaps that cut good
// tries short; the arm timer and the 45 deg cut-out still stop every run.
const unsigned long HEARTBEAT_MS = 2000;
// Step test (step 7): while armed, move the setpoint by STEP_TEST_DEG for
// STEP_TEST_MS, then back. The log shows how the loop responds to a sudden change.
const float STEP_TEST_DEG = 2.0;
const unsigned long STEP_TEST_MS = 1000;

// ---------- angle filter ----------
// Complementary filter weight: how much we trust the gyro each tick.
// 0.98 at 200 Hz means the accelerometer pulls the angle back with a time
// constant of about 0.25 s (dt * alpha / (1 - alpha) = 0.005 * 0.98 / 0.02).
// Higher = smoother but slower to correct drift. Change it live with "alpha N".
const float FILTER_ALPHA = 0.98;

// ---------- gyro bias calibration ----------
const int   CAL_SAMPLES     = 400;   // 2 s at 200 Hz; keep the robot still
const float CAL_MAX_SPREAD  = 4.0;   // deg/s; if the gyro wanders more than this, it moved
// The bias BB1 starts with, before (or without) a good calibration: the
// average of every log so far (2026-10-21 to 24). Measured X (pitch) values
// stayed within -0.48 to -0.75 deg/s, so this is off by at most about 0.15 deg/s.
// Through the filter that's under 0.05 deg of pitch: plenty to balance on.
// Why it matters: switched on standing up, BB1 is held by hand, the hand
// shakes, and the startup calibration fails. Without a starting bias it could
// not arm until it was set down still, which it can't do standing up.
const float GYRO_BIAS_TYPICAL[3] = { -0.60, 2.04, -2.24 };   // deg/s, x y z
