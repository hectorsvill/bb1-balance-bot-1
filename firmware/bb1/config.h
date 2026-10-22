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

// ---------- motors (step 5) ----------
const int PWM_FREQ = 20000;          // 20 kHz: above hearing, so the motors don't whine
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
// Starting values. Change them live with serial (kp, ki, kd, trim, cap, deadband)
// or the page's Balance panel. "save" stores them in flash (settings.h); saved
// values replace these at power-up, so after changing these type "defaults" then
// "save" (docs/PID_TUNING.md, "Two ways to change the gains").
// P, I, D gains like Betaflight's (pid.h explains each). Units: PWM per deg (P),
// PWM per deg*s (I), PWM per deg/s (D). Serial / page / logs call them kp, ki, kd.
const float DEFAULT_P_GAIN = 15.0;     // a guess to start from; tune per docs/PID_TUNING.md
const float DEFAULT_I_GAIN = 0.0;
const float DEFAULT_D_GAIN = 0.5;
const float DEFAULT_TRIM = -4.39;  // deg: the pitch where BB1 really balances (measured 2026-10-21; page: "Use current pitch")
const float DEFAULT_PWM_CAP = 80;  // PWM limit while armed (of 255). Gentle for first tests; raise once it balances.
const float DEFAULT_DEADBAND = 0;  // PWM added so small corrections still turn the wheels. Measure it.
const float ARM_ANGLE   = 10.0;    // deg: can only arm within this of the trim
const float FALL_ANGLE  = 45.0;    // deg: past this while armed = fallen, motors off
const unsigned long HEARTBEAT_MS = 1000;   // armed from the page: disarm if it goes quiet this long
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
