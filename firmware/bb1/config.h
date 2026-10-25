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
const float DEFAULT_SPEED_P = 4.0;
const float DEFAULT_SPEED_I = 0.5;
const float SPEED_SHIFT_MAX = 6.0;   // deg: the speed loop never moves the target more than this
const float SPEED_FILTER    = 0.05;  // per tick: smooths the wheel speed (about 0.1 s) so encoder steps don't kick the angle loop
// Heading hold (2026-10-24): keeps both wheels rolling the same distance, so BB1
// goes straight (the right wheel is about 7% faster at the same PWM). Unit: PWM
// moved from one wheel to the other per wheel turn of difference. 0 = off.
const float DEFAULT_TURN_HOLD = 20;
const float TURN_HOLD_MAX_PWM = 40;  // the steering correction never exceeds this
// Driving (milestone 2, drive.h): the page or the API asks for a wheel speed
// and a turn rate; drive.h moves the speed loop's and the heading hold's
// targets, and BB1 leans into it by itself. Like flying a quad in angle mode:
// you ask for a speed, the loops work out the lean. Needs sp/si and th on.
// Units are wheel turns per second (520 counts = 1 turn) until the wheel size is measured.
const float DRIVE_MAX_SPEED   = 1.0;   // turns/s, forward or back. On the floor BB1 stays under about 2; the lift detector trips at 4
const float DRIVE_MAX_TURN    = 1.0;   // turns/s of left-minus-right wheel difference (+ = turn right)
const float DRIVE_ACCEL       = 1.0;   // turns/s per second: how fast the speed target ramps (0 to full in 1 s)
const float DRIVE_TURN_ACCEL  = 2.0;   // the same for the turn target
// Drive feedforward (setting "dff", live on /tune, saved in flash): PWM per
// turn/s of speed target, added straight to both wheels. These motors are
// speed knobs: holding 1 turn/s takes about 105 PWM (first drive log,
// 2026-10-25). Without it the angle loop's I term has to wind up to that
// PWM, and when you reverse it has to unwind all of it first: meanwhile the
// wheels keep rolling the old way, BB1 tips back and falls. Betaflight's
// feedforward does the same job for the sticks.
const float DEFAULT_DRIVE_FF  = 60;
const float DRIVE_FF_MAX      = 150;
const float DRIVE_TURN_FF     = 15;    // PWM per turn/s of turn asked for, added straight to the steering (feedforward, like Betaflight's)
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
// Lift detector (control.h): wheels faster than this for this long while armed
// = picked up or in the air, motors off. On the floor it stays under about 2.
const float LIFT_TURNS_PER_SEC = 4.0;   // wheel turns per second, average of both wheels
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
