// control.h - the balance controller: arm / disarm, safety checks, the angle loop.
//
// Runs on core 1, once per tick, after the angle filter. Three states:
//
//   DISARMED --arm (upright, calibrated)--> ARMED --|pitch| > 45 deg--> FALLEN
//       ^                                     |                          |
//       +------- disarm / safety stop --------+---- disarm or re-arm ----+
//
// While ARMED, the PID turns the lean into a PWM for both wheels, and heading
// hold steers. Driving (drive.h) moves the speed loop's and heading hold's targets. Anything that looks wrong disarms at once: motors braked, STBY low
// (a fall, IMU errors, a late loop, a lift, a low battery, the page going away).
// The reason is kept in disarmReason so the page and serial can say why.
//
// Starting by itself (auto-arm) lives in auto_arm.h; this file only arms when asked.
//
// Settings come from shared.h (core 0 sets them from serial or the page) and
// are read fresh every tick, so a gain change applies on the next tick.

#pragma once
#include "config.h"
#include "shared.h"
#include "angle.h"
#include "battery.h"
#include "motors.h"
#include "pid.h"
#include "telemetry.h"
#include "drive.h"
#include "odom.h"

// Why the robot last disarmed, or why an arm request was refused.
enum Reason : uint8_t {
  REASON_NONE = 0,
  REASON_USER,           // Disarm button, space bar, serial "disarm" / Enter
  REASON_FELL,           // leaned past FALL_ANGLE while armed
  REASON_IMU,            // several IMU reads failed in a row
  REASON_HEARTBEAT,      // armed from the page, and the page stopped asking for /state
  REASON_OVERRUN,        // the loop ran late several ticks in a row
  REASON_NOT_UPRIGHT,    // arm refused: not within ARM_ANGLE of the trim
  REASON_NOT_CALIBRATED, // arm refused: no gyro bias yet (not used since 2026-10-24: BB1 starts with the typical bias)
  REASON_MOTOR_TEST,     // arm refused: a motor test is running
  REASON_CALIBRATE,      // disarmed to recalibrate the gyro
  REASON_PAGE_HIDDEN,    // the page was hidden (tab switched, phone locked) while armed
  REASON_TIMER,          // the arm time you set ran out
  REASON_LIFTED,         // wheels spun fast for a while: picked up, or in the air
  REASON_BATTERY,        // pack under BATTERY_LOW_V while a reading is present (armed cut-out, or arm refused)
  REASON_UPDATE,         // arm refused: a firmware update over Wi-Fi is running (ota.h)
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
    case REASON_LIFTED:         return "picked up (wheels spinning in the air)";
    case REASON_BATTERY:        return "battery low (under 3.3 V per cell)";
    case REASON_UPDATE:         return "a firmware update is running";
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
  float   speedPwm = 0;         // smode 1: what the wheel speed loop adds to the PWM this tick
  int32_t encAtArm = 0;         // average encoder count when it armed (position = since then)
  int32_t diffAtArm = 0;        // right minus left encoder when it armed (heading hold)
  bool    newArm = false;       // first tick after arming: remember the encoders
  int     fastTicks = 0;        // ticks in a row with the wheels far off the drive target (lift detector)
  bool    outSaturated = false; // last tick's motor PWM hit the cap (PID + feedforward + deadband): drive.h backs off
  int     encOddTicks = 0;      // ticks in a row with one encoder near 0 while the other spins (encoder fault detector)
  // Trim learn (tl, firmware 3.5): deg of lean added to the setpoint, learned
  // while standing still. Never reset by arming, disarming or driving, only at
  // boot (and set to 0 while tl is 0): what it learned about the balance
  // point stays true until the battery or the floor changes.
  float   trimLearned = 0;
};

Balance balance;                // only core 1 touches this

void balanceDisarm(uint8_t reason) {
  if (balance.armed) motorsOff();
  balance.armed = false;
  balance.pid.reset();
  driveUntilMs = 0;                             // a drive command never outlives the arm
  spinUntilMs = 0;                              // nor does a spin
  moveUntilMs = 0;                              // or a move
  driveReset();
  balance.stepUntilMs = 0;
  stepActive = false;
  disarmReason = reason;
  robotArmed = false;
}

// Called between ticks when core 0 asks to arm. Refuses (and says why) unless
// it's safe to start: no motor test, battery not low, near the balance point.
void balanceArm(float pitch, bool motorTestOn) {
  balance.fallen = false;
  if (motorTestOn)                              { disarmReason = REASON_MOTOR_TEST; return; }
  if (otaActive)                                { disarmReason = REASON_UPDATE; return; }   // flash writes pause this core (ota.h)
  // Same batteryTooLow() as auto-arm and the tick below: a low pack must not
  // start. No sense wire (under BATTERY_MIN_SENSE_V) is allowed.
  if (batteryTooLow())                          { disarmReason = REASON_BATTERY; return; }
  if (fabs(pitch - balanceTrim) > ARM_ANGLE)    { disarmReason = REASON_NOT_UPRIGHT; return; }
  balance.pid.reset();
  balance.imuErrorsInRow = 0;
  balance.fastTicks = 0;
  balance.encOddTicks = 0;
  encoderFault = 0;
  balance.overrunsInRow = 0;
  balance.armed = true;
  balance.armedAtMs = millis();
  balance.speedFiltered = 0;
  balance.newArm = true;                        // balanceTick() records the encoders on the next tick
  driveUntilMs = 0;                             // start standing still, whatever was asked before
  spinUntilMs = 0;
  moveUntilMs = 0;
  driveReset();
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
  //                 cancels a trim error, until the next drive moves home
  //                 (drive.h); trim learn (tl, below) keeps that correction
  // Both 0 = off. The log's setpoint column shows the result (trim + shift).
  // Driving (drive.h) gives both a target: speed error = speed - asked speed,
  // position error = position - where the target has rolled to.
  r.drive_speed = r.drive_turn = 0;              // 0 unless armed and driving
  r.drive_speed_now = r.drive_turn_now = r.speed_pwm = 0;
  r.spin_left_x10 = 0;
  if (!balance.armed) balance.speedPwm = 0;
  if (gainTrimLearn <= 0) balance.trimLearned = 0;  // tl 0 = off: forget it, exactly the 3.4 behavior
  if (balance.armed) {
    float speed = (r.speed_l + r.speed_r) / 2.0f;                       // counts/s, forward +
    balance.speedFiltered += SPEED_FILTER * (speed - balance.speedFiltered);
    int32_t encNow = (r.enc_l + r.enc_r) / 2;
    if (balance.newArm) { balance.encAtArm = encNow; balance.diffAtArm = r.enc_r - r.enc_l; balance.newArm = false; odomReset(a.yaw); }
    float turnsPerSec = balance.speedFiltered / COUNTS_PER_REV;
    float turns       = (float)(encNow - balance.encAtArm) / COUNTS_PER_REV;
    float diffTurns   = (float)((r.enc_r - r.enc_l) - balance.diffAtArm) / COUNTS_PER_REV;
    driveUpdate(dt, turns, diffTurns, balance.outSaturated, a.yawRate, turnsPerSec);
    odomUpdate(turns, a.yaw);                                          // where it is since arming (odom.h)
    // Encoder fault detector (config.h): going straight, both wheels should
    // read about the same. One near 0 while the other spins = not counting.
    float fastSide = max(abs(r.speed_l), abs(r.speed_r)), slowSide = min(abs(r.speed_l), abs(r.speed_r));
    bool odd = fabs(drive.turn) < 0.1f && fastSide > ENC_FAULT_MIN_SPEED && slowSide < ENC_FAULT_RATIO * fastSide;
    balance.encOddTicks = odd ? balance.encOddTicks + 1 : 0;
    if (balance.encOddTicks >= (int)(ENC_FAULT_MS * LOOP_HZ / 1000) && !encoderFault) {
      encoderFault = abs(r.speed_l) < abs(r.speed_r) ? 1 : 2;            // sticky until the next arm
      driveUntilMs = 0;   // also end the command already running (up to 3 s): ramp to a stop now
      spinUntilMs = 0;    // and a spin: it steers with the encoders too
      moveUntilMs = 0;    // and a move: it measures with them
    }
    if (encoderFault) r.flags |= FLAG_ENC_FAULT;
    if (drive.stopping) r.flags |= FLAG_DRIVE_STOPPING;
    static_assert(DRIVE_MAX_SPEED * DRIVE_LOG_STEPS <= 127 && DRIVE_MAX_TURN * DRIVE_TURN_LOG_STEPS <= 127,
                  "drive_speed/drive_turn are int8 in 1/DRIVE_LOG_STEPS (turn: 1/DRIVE_TURN_LOG_STEPS) turns/s: widen them before raising the limits");
    r.drive_speed = driveLogValue(drive.wantSpeed);   // log the operator's command (the ramped target follows from it and the # drive_ settings)
    r.drive_turn  = driveTurnLogValue(drive.wantTurn);
    // One int16 for whichever target is running (log format 15): a spin's deg x10,
    // or a move's mm with FLAG_MOVE set (the CSV splits them into spin_left / move_left).
    if (drive.moveOn) {
      r.spin_left_x10 = (int16_t)constrain(lroundf(moveLeftNow), -32000L, 32000L);
      r.flags |= FLAG_MOVE;
    } else {
      r.spin_left_x10 = (int16_t)constrain(lroundf(spinLeftNow * 10), -32000L, 32000L);   // deg still to turn, x10 (log format 14)
    }
    // Faster than asked -> lean back (negative); slower -> lean forward.
    // Drive speed I (sdi, config.h): while driving, keep adding lean for as
    // long as BB1 is slower than asked (less when faster). After a stop it
    // fades out instead of snapping, so the setpoint doesn't jump.
    if (drive.speed != 0) drive.lean += gainDriveSpeedI * (drive.speed - turnsPerSec) * dt;
    else                  drive.lean -= drive.lean * min(1.0f, dt / DRIVE_LEAN_FADE_S);
    // Both clamps use drive.leanLimit (drive.h): lmax while driving, easing
    // back to SPEED_SHIFT_MAX after a stop.
    drive.lean = constrain(drive.lean, -drive.leanLimit, drive.leanLimit);
    if (speedMode < 0.5f) {
      // Trim learn (tl, config.h): if BB1 rolls while it should stand still,
      // its real balance point isn't the trim. Rolling backward (speed -)
      // means the lean target is too far back: move it forward a little, by
      // tl deg per wheel turn rolled. This is the speed loop's I term (si),
      // except drive.pos is reset at every stop and this never is, so it
      // ends up equal to the trim error and stays fixed across commands.
      // Quad picture: Betaflight's accelerometer trim, or a position hold
      // that remembers the wind instead of fighting it again after every
      // stick input. Only standing still: while driving, ramping down, or
      // in the coast and I-relax after a stop, rolling is expected, not a
      // trim error. Not during a step test either (it leans on purpose).
      bool standingStill = drive.speed == 0 && drive.turn == 0 && !drive.stopping &&
                           drive.relaxLeft <= 0 && !balance.stepUntilMs;
      if (standingStill && gainTrimLearn > 0) balance.trimLearned -= gainTrimLearn * turnsPerSec * dt;
      balance.trimLearned = constrain(balance.trimLearned, -TRIM_LEARN_MAX, TRIM_LEARN_MAX);
      // Acceleration feedforward (aff, drive.h): while the speed target ramps,
      // lean into it right away (forward + while speeding up forward, back
      // while slowing), about 1.04 deg per turn/s^2 by physics. sp and sdi
      // only lean once BB1 is already behind; this leans before it falls behind.
      float shift = -(gainSpeedP * (turnsPerSec - drive.speed) + gainSpeedI * (turns - drive.pos)) + drive.lean + drive.accelLean +
                    balance.trimLearned;
      setpoint += constrain(shift, -drive.leanLimit, drive.leanLimit);
      balance.speedPwm = 0;
    } else {
      // smode 1: the wheel speed loop. Faster than asked, or ahead of the target
      // position -> more forward PWM (see the sign note in config.h).
      drive.lean = 0;
      float pwm = gainWheelSpeedP * (turnsPerSec - drive.speed) + gainWheelSpeedI * (turns - drive.pos);
      balance.speedPwm = constrain(pwm, -WHEEL_SPEED_PWM_MAX, WHEEL_SPEED_PWM_MAX);
    }
    r.drive_speed_now = driveLogValue(drive.speed);   // the ramped targets BB1 really aims for (log format 11)
    r.drive_turn_now  = driveTurnLogValue(drive.turn);
    r.speed_pwm       = (int8_t)lroundf(balance.speedPwm);
  }
  trimLearnedNow = balance.trimLearned;         // for /state (kept while disarmed: it is still what BB1 learned)
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
    // Lift detector: lifted, the wheels push on nothing and race up to full
    // speed while the body stays upright, so the 45 deg check never fires.
    // On the floor the wheels follow the drive target closely (within about
    // 1 turn/s, 2026-10-29 logs), so the test is how far each wheel is from
    // its own target: drive.speed, plus or minus half the turn (turn is
    // left minus right). Standing still both targets are 0 and this is the
    // plain wheel speed, the same check as before 3.6. Driving at 3 turns/s
    // it takes both wheels at about 7 turns/s (an average 4 off) to count.
    float targetL = drive.speed + drive.turn / 2, targetR = drive.speed - drive.turn / 2;
    float wheelErr = (fabsf((float)r.speed_l / COUNTS_PER_REV - targetL) +
                      fabsf((float)r.speed_r / COUNTS_PER_REV - targetR)) / 2.0f;
    balance.fastTicks = wheelErr > LIFT_TURNS_PER_SEC ? balance.fastTicks + 1 : 0;
    // Low battery first, like a quad's battery failsafe. On the floor the motors
    // act like speed knobs, so a sagging pack would keep driving until something
    // else tripped. Checked before the fall so a dying pack isn't counted as a
    // fall (auto-arm would stand it up and start again). No sense wire: batteryTooLow()
    // is false under BATTERY_MIN_SENSE_V, same exemption as auto-arm.
    if (batteryTooLow())                               balanceDisarm(REASON_BATTERY);
    else if (fabs(a.pitch - balanceTrim) > FALL_ANGLE) { balance.fallen = true; balanceDisarm(REASON_FELL); }
    else if (balance.imuErrorsInRow >= 5)          balanceDisarm(REASON_IMU);
    else if (balance.overrunsInRow >= 5)           balanceDisarm(REASON_OVERRUN);
    else if (balance.fastTicks >= (int)(LIFT_MS * LOOP_HZ / 1000)) balanceDisarm(REASON_LIFTED);
    else if (armedFromWeb && webAgeMs > (int32_t)HEARTBEAT_MS)  balanceDisarm(REASON_HEARTBEAT);
    else if (armTimeMs && millis() - balance.armedAtMs >= armTimeMs) balanceDisarm(REASON_TIMER);
  }

  if (!balance.armed) {
    r.p_x10 = r.i_x10 = r.d_x10 = 0;
    balance.outSaturated = false;
    return 0;
  }

  // ---- the angle loop ----
  Pid &pid = balance.pid;
  pid.gainP = gainP;  pid.gainI = gainI;  pid.gainD = gainD;
  pid.limit = pwmCap;
  float error = a.pitch - setpoint;             // forward lean past the balance point = positive
  // I-term relax (irelax, config.h): just after a drive, drain the I term
  // toward 0 so it can't keep the wheels rolling the old way. Before the
  // update, so this tick's P, I, D already use the drained value.
  if (drive.relaxLeft > 0 && gainIRelax > 0) {
    pid.integral *= 1.0f - min(1.0f, dt / gainIRelax);
    drive.relaxLeft -= dt;
    r.flags |= FLAG_I_RELAX;
  }
  float out = pid.update(error, a.pitchRate, dt);
  // Drive feedforward: while driving, the PWM that speed needs goes straight
  // in, so the I term only fixes what's left (and has little to unwind when
  // you reverse). 0 when standing still. Not in the p/i/d log columns: pwm_l/r include it.
  out += gainDriveFF * drive.speed;
  out += balance.speedPwm;                      // smode 1: the wheel speed loop (0 in smode 0). Not in p/i/d either

  // Deadband: added per wheel after the steering (wheelPwm() below), not here.
  // For the saturation flag, count it the way the busier wheel will get it.
  float withDb = fabsf(out) > 0.5f ? fabsf(out) + motorDeadband : fabsf(out);
  // Saturated = the PID alone hit the cap, or the PID plus the drive
  // feedforward and deadband did (log format 12; before, only the PID alone
  // counted, which missed the case that matters when driving fast: dff x
  // speed eats the cap and the clamp below cuts the balance correction).
  balance.outSaturated = pid.saturated || withDb >= pwmCap;
  out = constrain(out, -pwmCap, pwmCap);

  if (balance.outSaturated) r.flags |= FLAG_SATURATED;
  r.p_x10 = (int16_t)constrain(pid.p * 10, -32000.0f, 32000.0f);
  r.i_x10 = (int16_t)constrain(pid.i * 10, -32000.0f, 32000.0f);
  r.d_x10 = (int16_t)constrain(pid.d * 10, -32000.0f, 32000.0f);
  return (int)lroundf(out);
}

// One wheel's PWM: balance + or - steering, then the deadband, then the cap.
// Deadband: the motors don't turn at all below some PWM, so add it on top so a
// small correction still moves the wheel. Per wheel since firmware 3.7: before,
// it was added to the shared balance output, so while turning, every time the
// balance output crossed 0 (about 11 times a second) both wheels jumped by
// 2 x deadband, and the slow wheel stuck and slipped near 0: the turn jitter
// (run 178, 2026-10-31). Now each wheel gets it in the direction it really
// turns. Standing still with no steering it is the same as before.
int wheelPwm(float pwm, int cap) {
  if (pwm > 0.5f)       pwm += motorDeadband;
  else if (pwm < -0.5f) pwm -= motorDeadband;
  return (int)constrain(lroundf(pwm), (long)-cap, (long)cap);
}

// Heading hold: how much PWM to move from the right wheel to the left (or back)
// so both wheels roll the same distance since arming (or, while turning, the
// difference drive.h asks for). The right wheel runs
// about 7% faster at the same PWM, so without this BB1 curves left. If the
// right wheel is ahead by one wheel turn, the left gets +th PWM and the right
// -th PWM. Turning, drive.turn adds DRIVE_TURN_FF PWM per turn/s at once
// (right turn = more left wheel). Call after balanceTick() on the same tick;
// 0 when disarmed. th 0 = no hold, but turning still works on feedforward alone.
int balanceSteer(const LogRecord &r) {
  if (!balance.armed || encoderFault) return 0;   // bad counts would steer it in circles
  float aheadTurns = (float)((r.enc_r - r.enc_l) - balance.diffAtArm) / COUNTS_PER_REV - drive.diff;   // + = right wheel ahead of the target
  float hold = gainTurnHold > 0 ? gainTurnHold * aheadTurns : 0;
  float feedforward = DRIVE_TURN_FF * drive.turn;
  return (int)lroundf(constrain(hold + feedforward, -TURN_HOLD_MAX_PWM, TURN_HOLD_MAX_PWM));
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
