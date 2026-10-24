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
  unsigned long stepUntilMs = 0; // step test: setpoint is trim + STEP_TEST_DEG until then (0 = off)
  // Speed loop (step 8)
  float   speedFiltered = 0;    // wheel speed, counts/s, smoothed
  int32_t encAtArm = 0;         // average encoder count when it armed (position = since then)
  int32_t diffAtArm = 0;        // right minus left encoder when it armed (heading hold)
  bool    newArm = false;       // first tick after arming: remember the encoders
};

Balance balance;                // only core 1 touches this

void balanceDisarm(uint8_t reason) {
  if (balance.armed) motorsOff();
  balance.armed = false;
  balance.pid.reset();
  balance.stepUntilMs = 0;
  stepActive = false;
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
  balance.speedFiltered = 0;
  balance.newArm = true;                        // balanceTick() records the encoders on the next tick
  armedSinceMs = balance.armedAtMs;
  robotArmed = true;
  disarmReason = REASON_NONE;
  motorsEnable(true);                           // wake the driver; balanceTick() sets the PWM
}

// One tick of the angle loop. Returns the PWM to send to both wheels (0 when
// disarmed) and fills the controller fields of the log record.
int balanceTick(const Angle &a, bool imuOk, bool overran, float dt, LogRecord &r) {
  // The setpoint is the trim, plus STEP_TEST_DEG while a step test runs.
  if (balance.stepUntilMs && millis() >= balance.stepUntilMs) { balance.stepUntilMs = 0; stepActive = false; }
  float setpoint = balanceTrim + (balance.stepUntilMs ? STEP_TEST_DEG : 0);

  // ---- the speed loop (step 8) ----
  // The angle loop alone keeps BB1 at the trim angle, but any small trim error
  // means a steady lean, and a steady lean means accelerating that way forever:
  // it rolls faster and faster until the motors max out (TUNING_LOG rounds
  // 16-20). This outer loop watches the wheels. Rolling forward -> move the
  // target angle back a little, so the angle loop drives the wheels ahead of
  // the body, BB1 tips back and brakes. Like position hold on a quad.
  //   speed P (sp): degrees back per wheel turn per second
  //   speed I (si): degrees back per wheel turn rolled since arming (the
  //                 integral of speed = position): pulls it back home and
  //                 cancels a trim error for good
  // Both 0 = off. The log's setpoint column shows the result (trim + shift).
  if (balance.armed) {
    float speed = (r.speed_l + r.speed_r) / 2.0f;                       // counts/s, forward +
    balance.speedFiltered += SPEED_FILTER * (speed - balance.speedFiltered);
    int32_t encNow = (r.enc_l + r.enc_r) / 2;
    if (balance.newArm) { balance.encAtArm = encNow; balance.diffAtArm = r.enc_r - r.enc_l; balance.newArm = false; }
    float turnsPerSec = balance.speedFiltered / COUNTS_PER_REV;
    float turns       = (float)(encNow - balance.encAtArm) / COUNTS_PER_REV;
    float shift = -(gainSpeedP * turnsPerSec + gainSpeedI * turns);    // rolling forward -> lean back (negative)
    setpoint += constrain(shift, -SPEED_SHIFT_MAX, SPEED_SHIFT_MAX);
  }
  r.setpoint_cdeg = (int16_t)(setpoint * 100);
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
  pid.gainP = gainP;  pid.gainI = gainI;  pid.gainD = gainD;
  pid.limit = pwmCap;
  float error = a.pitch - setpoint;             // forward lean past the balance point = positive
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

// Heading hold: how much PWM to move from the right wheel to the left (or back)
// so both wheels roll the same distance since arming. The right wheel runs
// about 7% faster at the same PWM, so without this BB1 curves left. If the
// right wheel is ahead by one wheel turn, the left gets +th PWM and the right
// -th PWM. Call after balanceTick() on the same tick; 0 when disarmed or th 0.
int balanceSteer(const LogRecord &r) {
  if (!balance.armed || gainTurnHold <= 0) return 0;
  float aheadTurns = (float)((r.enc_r - r.enc_l) - balance.diffAtArm) / COUNTS_PER_REV;   // + = right wheel ahead
  return (int)lroundf(constrain(gainTurnHold * aheadTurns, -TURN_HOLD_MAX_PWM, TURN_HOLD_MAX_PWM));
}

// Called between ticks when core 0 asks for a step test. Only while armed.
void balanceStep() {
  if (!balance.armed) return;
  balance.stepUntilMs = millis() + STEP_TEST_MS;
  stepActive = true;
}

uint8_t balanceMode() {
  if (balance.armed)  return MODE_ARMED;
  if (balance.fallen) return MODE_FALLEN;
  return MODE_RUNNING;
}
