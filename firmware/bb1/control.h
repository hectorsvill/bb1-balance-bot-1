// control.h - the balance controller: arm / disarm, safety checks, the angle loop.
//
// Runs on core 1, once per tick, after the angle filter. Three states:
//
//   DISARMED --arm (upright, calibrated)--> ARMED --|pitch| > 45 deg--> FALLEN
//       ^                                     |                          |
//       +------- disarm / safety stop --------+---- disarm or re-arm ----+
//
// While ARMED, the PID turns the lean into a PWM for both wheels (no steering
// yet). Anything that looks wrong disarms at once: motors braked, STBY low.
// The reason is kept in disarmReason so the page and serial can say why.
//
// Settings come from shared.h (core 0 sets them from serial or the page) and
// are read fresh every tick, so a gain change applies on the next tick.

#pragma once
#include "config.h"
#include "shared.h"
#include "angle.h"
#include "motors.h"
#include "pid.h"
#include "telemetry.h"

// Why the robot last disarmed, or why an arm request was refused.
enum Reason : uint8_t {
  REASON_NONE = 0,
  REASON_USER,           // Disarm button, space bar, serial "disarm" / Enter
  REASON_FELL,           // leaned past FALL_ANGLE while armed
  REASON_IMU,            // several IMU reads failed in a row
  REASON_HEARTBEAT,      // armed from the page, and the page stopped asking for /state
  REASON_OVERRUN,        // the loop ran late several ticks in a row
  REASON_NOT_UPRIGHT,    // arm refused: not within ARM_ANGLE of the trim
  REASON_NOT_CALIBRATED, // arm refused: no gyro bias yet
  REASON_MOTOR_TEST,     // arm refused: a motor test is running
  REASON_CALIBRATE,      // disarmed to recalibrate the gyro
  REASON_PAGE_HIDDEN,    // the page was hidden (tab switched, phone locked) while armed
  REASON_TIMER,          // the arm time you set ran out
};

const char *reasonText(uint8_t r) {
  switch (r) {
    case REASON_USER:           return "disarmed by you";
    case REASON_FELL:           return "fell over (leaned past 45 deg)";
    case REASON_IMU:            return "IMU stopped answering";
    case REASON_HEARTBEAT:      return "lost the web page (no /state for 1 s)";
    case REASON_OVERRUN:        return "control loop running late";
    case REASON_NOT_UPRIGHT:    return "not upright: hold it near the balance point to arm";
    case REASON_NOT_CALIBRATED: return "gyro not calibrated yet";
    case REASON_MOTOR_TEST:     return "a motor test is running";
    case REASON_CALIBRATE:      return "disarmed to calibrate";
    case REASON_PAGE_HIDDEN:    return "the page was hidden (tab switched or screen locked)";
    case REASON_TIMER:          return "arm time finished";
    default:                    return "";
  }
}

struct Balance {
  bool armed = false;
  bool fallen = false;          // set when it falls; cleared by the next arm or disarm
  Pid  pid;
  int  imuErrorsInRow = 0;
  int  overrunsInRow = 0;
  unsigned long armedAtMs = 0;  // millis() when it armed, for the arm timer
};

Balance balance;                // only core 1 touches this

void balanceDisarm(uint8_t reason) {
  if (balance.armed) motorsOff();
  balance.armed = false;
  balance.pid.reset();
  disarmReason = reason;
  robotArmed = false;
}

// Called between ticks when core 0 asks to arm. Refuses (and says why) unless
// it's safe to start: no motor test, gyro calibrated, near the balance point.
void balanceArm(float pitch, bool motorTestOn) {
  balance.fallen = false;
  if (motorTestOn)                              { disarmReason = REASON_MOTOR_TEST; return; }
  if (!everCalibrated)                          { disarmReason = REASON_NOT_CALIBRATED; return; }
  if (fabs(pitch - balanceTrim) > ARM_ANGLE)    { disarmReason = REASON_NOT_UPRIGHT; return; }
  balance.pid.reset();
  balance.imuErrorsInRow = 0;
  balance.overrunsInRow = 0;
  balance.armed = true;
  balance.armedAtMs = millis();
  robotArmed = true;
  disarmReason = REASON_NONE;
  motorsEnable(true);                           // wake the driver; balanceTick() sets the PWM
}

// One tick of the angle loop. Returns the PWM to send to both wheels (0 when
// disarmed) and fills the controller fields of the log record.
int balanceTick(const Angle &a, bool imuOk, bool overran, float dt, LogRecord &r) {
  r.setpoint_cdeg = (int16_t)(balanceTrim * 100);
  r.gains_ver = (uint16_t)gainsVersion;
  r.reason = disarmReason;                      // the value before this tick's checks; next tick shows any new one

  // How long since the page last polled /state. Read the shared time once, and
  // compare as a signed number: core 0 may set it a moment after we read
  // millis(), and "now - later" must come out as a small negative, not a huge
  // unsigned number that looks like a 49-day silence.
  uint32_t lastWeb = lastWebHeartbeatMs;
  int32_t webAgeMs = (int32_t)(millis() - lastWeb);
  r.web_age = (uint8_t)constrain(webAgeMs / 10, 0L, 255L);

  if (balance.armed) {
    // ---- safety checks first, every tick ----
    balance.imuErrorsInRow = imuOk ? 0 : balance.imuErrorsInRow + 1;
    balance.overrunsInRow  = overran ? balance.overrunsInRow + 1 : 0;
    if (fabs(a.pitch - balanceTrim) > FALL_ANGLE) { balance.fallen = true; balanceDisarm(REASON_FELL); }
    else if (balance.imuErrorsInRow >= 5)          balanceDisarm(REASON_IMU);
    else if (balance.overrunsInRow >= 5)           balanceDisarm(REASON_OVERRUN);
    else if (armedFromWeb && webAgeMs > (int32_t)HEARTBEAT_MS)  balanceDisarm(REASON_HEARTBEAT);
    else if (armTimeMs && millis() - balance.armedAtMs >= armTimeMs) balanceDisarm(REASON_TIMER);
  }

  if (!balance.armed) {
    r.p_x10 = r.i_x10 = r.d_x10 = 0;
    return 0;
  }

  // ---- the angle loop ----
  Pid &pid = balance.pid;
  pid.kp = gainKp;  pid.ki = gainKi;  pid.kd = gainKd;
  pid.limit = pwmCap;
  float error = a.pitch - balanceTrim;          // forward lean past the balance point = positive
  float out = pid.update(error, a.pitchRate, dt);

  // Deadband: the motors don't turn at all below some PWM. Add it on top so a
  // small correction still moves the wheels, then clamp to the cap again.
  if (out > 0.5f)       out += motorDeadband;
  else if (out < -0.5f) out -= motorDeadband;
  out = constrain(out, -pwmCap, pwmCap);

  if (pid.saturated) r.flags |= FLAG_SATURATED;
  r.p_x10 = (int16_t)constrain(pid.p * 10, -32000.0f, 32000.0f);
  r.i_x10 = (int16_t)constrain(pid.i * 10, -32000.0f, 32000.0f);
  r.d_x10 = (int16_t)constrain(pid.d * 10, -32000.0f, 32000.0f);
  return (int)lroundf(out);
}

uint8_t balanceMode() {
  if (balance.armed)  return MODE_ARMED;
  if (balance.fallen) return MODE_FALLEN;
  return MODE_RUNNING;
}
