// shared.h - how core 0 (serial, later Wi-Fi) asks core 1 (control) to do things.
//
// Rule of the house: core 1 owns the IMU and the motors. Core 0 never
// touches them directly; it sets a request here and core 1 acts on it at the
// start of its next tick. That way nothing ever happens halfway through a tick.
//
// "volatile" makes each core really re-read these from memory instead of
// keeping a stale copy in a register. Each is 32 bits or smaller, and the
// RP2040 reads and writes those in one go, so no lock is needed for them.
// (Bigger things, like a whole LogRecord, go through the mutex in telemetry.h.)

#pragma once
#include "config.h"

// Requests from core 0 to core 1
volatile bool  requestCalibrate = false;   // core 1 runs imuCalibrate(), then clears it
volatile float requestedAlpha   = FILTER_ALPHA;

// Motor test (step 5). Core 0 fills in the three numbers FIRST, then sets
// requestMotorTest; core 1 sees the flag and reads them. Forward is positive.
volatile int16_t  testLeftPwm  = 0;        // -MOTOR_TEST_MAX_PWM..+MOTOR_TEST_MAX_PWM
volatile int16_t  testRightPwm = 0;
volatile uint16_t testMs       = 0;        // how long, at most MOTOR_TEST_MAX_MS
volatile bool     requestMotorTest = false;
volatile bool     requestMotorStop = false; // stop now (page Stop button, space bar, serial "stop")

// Status from core 1 to core 0
volatile bool     imuFound       = false;
volatile uint32_t calibrationsDone = 0;    // counts up after each calibration attempt
volatile bool     lastCalibrationOk = false;
volatile bool     everCalibrated = false;  // true once any calibration has succeeded since boot
volatile bool     motorTestRunning = false;
volatile uint32_t motorTestsDone = 0;      // counts up when a test ends
volatile int32_t  lastTestMovedL = 0;      // encoder counts each wheel moved in the last test
volatile int32_t  lastTestMovedR = 0;
volatile uint32_t lastTestRanMs  = 0;
