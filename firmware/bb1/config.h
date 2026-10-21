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

// IMU: the chip's X-axis tilt reads NEGATIVE when the robot leans forward,
// so we multiply by -1 to make "forward lean" positive.
// The gyro X rate should need the same flip. If, while you tilt forward, the
// filtered pitch and the accel-only pitch move in opposite directions,
// change GYRO_PITCH_SIGN to +1.
const float ACCEL_PITCH_SIGN = -1.0;
const float GYRO_PITCH_SIGN  = -1.0;

// ---------- timing ----------
const int LOOP_HZ = 200;                          // control loop rate
const unsigned long LOOP_US = 1000000UL / LOOP_HZ; // 5000 us = 5 ms per tick
const int PRINT_HZ = 10;                          // how often the serial readout updates

// ---------- angle filter ----------
// Complementary filter weight: how much we trust the gyro each tick.
// 0.98 at 200 Hz means the accelerometer pulls the angle back with a time
// constant of about 0.25 s (dt * alpha / (1 - alpha) = 0.005 * 0.98 / 0.02).
// Higher = smoother but slower to correct drift. Change it live with "alpha N".
const float FILTER_ALPHA = 0.98;

// ---------- gyro bias calibration ----------
const int   CAL_SAMPLES     = 400;   // 2 s at 200 Hz; keep the robot still
const float CAL_MAX_SPREAD  = 4.0;   // deg/s; if the gyro wanders more than this, it moved
