// shared.h - how core 0 (serial, later Wi-Fi) asks core 1 (control) to do things.
//
// Rule of the house: core 1 owns the IMU (and later the motors). Core 0 never
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

// Status from core 1 to core 0
volatile bool     imuFound       = false;
volatile uint32_t calibrationsDone = 0;    // counts up after each calibration attempt
volatile bool     lastCalibrationOk = false;
volatile bool     everCalibrated = false;  // true once any calibration has succeeded since boot
