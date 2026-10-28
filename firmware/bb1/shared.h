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

// Balancing (step 6). Core 0 sets these; core 1 reads them every tick.
// Each is 32 bits, so a tick never sees half a number. gainsVersion counts
// every change, and goes in each log record so a run shows which gains it used.
// P, I, D gains (Kp, Ki, Kd), set from serial or the page; core 1 copies them into the PID each tick.
volatile float    gainP = DEFAULT_P_GAIN, gainI = DEFAULT_I_GAIN, gainD = DEFAULT_D_GAIN;
volatile float    balanceTrim   = DEFAULT_TRIM;
volatile float    pwmCap        = DEFAULT_PWM_CAP;
volatile float    motorDeadband = DEFAULT_DEADBAND;
volatile float    gainSpeedP    = DEFAULT_SPEED_P;   // speed loop (control.h), deg per wheel turn/s
volatile float    gainSpeedI    = DEFAULT_SPEED_I;   // speed loop, deg per wheel turn rolled since arming
volatile float    gainTurnHold  = DEFAULT_TURN_HOLD; // heading hold, PWM per wheel turn of left/right difference
volatile float    gainDriveFF   = DEFAULT_DRIVE_FF;  // drive feedforward, PWM per turn/s of speed target (drive.h)
volatile float    gainDriveSpeedI = DEFAULT_DRIVE_SPEED_I;  // drive speed I, deg per turn/s per s (control.h)
volatile float    gainIRelax      = DEFAULT_I_RELAX_S;      // I-term relax after a drive, s time constant, 0 = off (control.h)
volatile float    speedMode       = DEFAULT_SPEED_MODE;     // 0 = speed loop moves the setpoint, 1 = wheel speed loop on the PWM (config.h)
volatile float    gainWheelSpeedP = DEFAULT_WHEEL_SPEED_P;  // smode 1: PWM per turn/s too fast
volatile float    gainWheelSpeedI = DEFAULT_WHEEL_SPEED_I;  // smode 1: PWM per wheel turn ahead
volatile float    autoArm       = DEFAULT_AUTO_ARM;  // 1 = start balancing by itself when stood upright (control.h)
volatile uint32_t gainsVersion  = 1;
volatile bool     requestArm    = false;
volatile bool     requestDisarm = false;
volatile uint8_t  requestDisarmReason = 1;   // REASON_USER unless the page says it was hidden
volatile bool     armedFromWeb  = false;     // armed from the page: the heartbeat check applies
volatile uint32_t armTimeMs     = 0;         // disarm by itself after this long armed; 0 = no limit
volatile bool     requestStep   = false;     // step test: core 1 moves the setpoint for STEP_TEST_MS
volatile bool     stepActive    = false;     // status: a step test is running
volatile uint32_t lastWebHeartbeatMs = 0;    // millis() of the page's latest /state request
volatile uint16_t batteryMv     = 0;         // battery voltage in mV, from core 0 (battery.h); core 1 logs it
// Firmware update over Wi-Fi (ota.h). Core 0 sets otaActive when an upload
// starts; core 1 then refuses every arm and motor test, and answers with
// otaMotorsOff once nothing drives the motors. Writing flash pauses core 1.
volatile bool     otaActive     = false;
volatile bool     otaMotorsOff  = false;

// Driving (milestone 2, drive.h). Core 0 writes the two numbers FIRST, then
// driveUntilMs; core 1 uses them until millis() passes driveUntilMs, then
// ramps back to standing still. driveUntilMs = 0 = stop driving now.
volatile float    driveSpeedCmd = 0;         // wheel turns/s asked for, forward +
volatile float    driveTurnCmd  = 0;         // turns/s of left-minus-right wheel difference, right turn +
volatile uint32_t driveUntilMs  = 0;         // millis() when the command runs out
volatile uint32_t driveCommands = 0;         // counts every accepted command (shown in /state)
// What core 1 is actually doing (after the ramp), for /state
volatile float    driveSpeedNow = 0;
volatile float    driveTurnNow  = 0;

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
volatile bool     robotArmed     = false;   // the balance loop is driving the motors
volatile uint32_t armedSinceMs   = 0;       // millis() when it last armed (for the page's countdown)
volatile uint8_t  disarmReason   = 0;       // Reason in control.h: why it last disarmed / refused to arm
volatile uint32_t armAttempts    = 0;       // counts up after each arm request is handled
volatile uint8_t  autoArmState   = 4;       // AutoState in auto_arm.h (4 = waiting for upright)
volatile uint8_t  encoderFault   = 0;       // control.h: 1 = left encoder not counting, 2 = right (while armed; cleared on arm)
