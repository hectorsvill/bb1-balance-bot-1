// drive.h - driving from the page or the API (milestone 2).
//
// BB1 doesn't drive by sending PWM to the wheels: the balance loop owns them.
// Instead it moves the targets of the two outer loops in control.h:
//
//   speed loop (sp/si)  holds the wheels at a speed and a position.
//                       Standing still the speed target is 0 and the position
//                       target is where it stopped. Driving, the speed target
//                       is the asked speed and the position target rides with
//                       the wheels, so the speed I term doesn't pull BB1 back
//                       (or push it on) after you let go.
//   heading hold (th)   holds the right-minus-left wheel difference.
//                       Turning, that target slides at the asked turn rate,
//                       and DRIVE_TURN_FF adds the steering PWM straight away
//                       (feedforward) so the hold doesn't have to lag first.
//
// The speed loop then leans BB1 forward to speed up and back to slow down,
// all by itself. Quad picture: angle mode with position hold, and the sticks
// move the hold point instead of the motors.
//
// Safety: every command runs out after DRIVE_HOLD_MS (or its own ms=, at most
// DRIVE_HOLD_MAX_MS). The page sends one every 100 ms while you hold the
// pad; if Wi-Fi drops or the page closes, the command runs out and BB1 ramps
// to a stop and keeps balancing where it is. It never disarms for that.
// Targets ramp (dacc for speed, DRIVE_TURN_ACCEL for turn) so a full-stick
// jab can't kick it over. Slowing down is never slower than
// DRIVE_STOP_ACCEL_MIN, so a stop from full speed stays short even at a low dacc. While the speed target ramps, the acceleration
// feedforward (aff, control.h) leans BB1 into it, and the lean limit is
// lmax instead of the standing SPEED_SHIFT_MAX (leanLimit below).
//
// Exact-angle spins (firmware 3.10): POST /spin deg=90 rate=2 turns on the spot
// by an angle instead of for a time (spinUpdate() below).
//
// Runs on core 1 inside balanceTick(). Core 0 only writes the command in shared.h.

#pragma once
#include "config.h"
#include "shared.h"

struct Drive {
  float wantSpeed = 0;  // the command in force this tick (0 once it runs out): what gets logged
  float wantTurn  = 0;
  float speed = 0;      // speed target after the ramp, turns/s, forward +
  float turn  = 0;      // turn target after the ramp, turns/s, right +
  float pos   = 0;      // position target, wheel turns since arming
  float diff  = 0;      // heading target, right-minus-left wheel turns since arming
  bool  stopping = false; // the command ran out (or was stopped) and it is ramping down
  float lean  = 0;      // deg the speed I (sdi) has built up while driving (control.h)
  float relaxLeft = 0;  // s left of the I-term relax after a drive (irelax, control.h)
  float accel = 0;      // turns/s per s the speed target moved this tick: +dacc, -dacc or 0
  float accelLean = 0;  // deg: aff x accel, smoothed (ACCEL_FF_SMOOTH_S); control.h adds it to the shift
  float leanLimit = SPEED_SHIFT_MAX;  // deg: the clamp on the speed loop's setpoint shift this tick
  // Fast drive step 2 (firmware 3.6): the two speed guards (speedCeiling() below).
  float satSeconds = 0;               // s the motor PWM has sat at the cap in a row while driving
  float ceiling = DRIVE_MAX_SPEED;    // turns/s: the "too fast, back off" guard's limit on |speed target|
  // Exact-angle spin (spinUpdate() below)
  bool     spinOn      = false;  // a spin is running (turning or ramping down)
  bool     spinBraking = false;  // the turn target is ramping down to land on the angle
  float    spinSettle  = -1;     // s left of the settle after landing (-1 = not settling yet)
  float    spinLeft    = 0;      // deg still to turn, right + (counts down by the gyro yaw)
  float    spinTurn    = 0;      // the turn target while spinning, turns/s, right +
  uint32_t spinSeen    = 0;      // the last spinStarts this core acted on
};

Drive drive;            // only core 1 touches this

// Core 0 (POST /drive and serial "drive") hands a command to core 1 here, so
// both paths clamp the same way. Returns false for a value that isn't a
// number (speed=nan would get through constrain(): every comparison with NaN
// is false). ms 0 = stop now. Speed and turn are written before the deadline:
// core 1 only reads them once it sees the new deadline.
bool driveCommand(float &speed, float &turn, long &ms) {
  if (!isfinite(speed) || !isfinite(turn)) return false;
  speed = constrain(speed, -DRIVE_MAX_SPEED, DRIVE_MAX_SPEED);
  turn  = constrain(turn,  -DRIVE_MAX_TURN,  DRIVE_MAX_TURN);
  ms    = constrain(ms, 0L, (long)DRIVE_HOLD_MAX_MS);
  spinUntilMs   = 0;            // a drive command takes over from a spin
  driveSpeedCmd = speed;
  driveTurnCmd  = turn;
  driveUntilMs  = ms ? millis() + ms : 0;
  driveCommands = driveCommands + 1;
  return true;
}

// Core 0 (POST /spin and serial "spin") hands a spin to core 1. deg: right +.
// rate: turns/s of wheel difference (the /drive turn unit), clamped. Returns
// the timeout in ms, or 0 for a value it refuses (not a number, too small).
unsigned long spinCommand(float &deg, float &rate) {
  if (!isfinite(deg) || !isfinite(rate)) return 0;
  if (fabsf(deg) < SPIN_MIN_DEG) return 0;
  deg  = constrain(deg, -SPIN_MAX_DEG, SPIN_MAX_DEG);
  rate = constrain(fabsf(rate), SPIN_MIN_RATE, DRIVE_MAX_TURN);
  // Timeout: twice the planned time plus a margin. A spin that hasn't landed
  // by then (stuck wheel, lifted) ramps to a stop anyway.
  unsigned long ms = (unsigned long)(2000.0f * fabsf(deg) / (rate * SPIN_DEG_PER_TURN)) + SPIN_EXTRA_MS;
  driveUntilMs = 0;             // a spin takes over from a drive command
  spinDegCmd   = deg;
  spinRateCmd  = rate;
  spinUntilMs  = millis() + ms;
  spinStarts   = spinStarts + 1;   // last: core 1 starts when it sees this change
  return ms;
}

// On every arm: stand still where it is. A spin asked for before this arm
// is ignored (spinSeen catches up), so nothing old starts moving.
void driveReset() {
  uint32_t seen = spinStarts;
  drive = Drive();
  drive.spinSeen = seen;
  driveSpeedNow = driveTurnNow = 0;
  spinLeftNow = 0;
}

// One tick of an exact-angle spin. yawRate: deg/s from the gyro, right +.
// Returns the turn target the spin wants this tick (0 when not spinning).
//
// How it lands on the angle: the turn target ramps down at DRIVE_TURN_ACCEL,
// so stopping from yaw rate w takes t = turn / DRIVE_TURN_ACCEL seconds and
// turns about w x t / 2 more degrees (the area under a straight-line ramp).
// Every tick it compares that stopping angle with the angle still to go; the
// first tick it's not smaller, it starts the ramp-down. It uses the real yaw
// rate, so wheel scrub and the floor don't matter, and it's all on core 1, so
// Wi-Fi timing doesn't either (the Mac timing spins was 0.2-0.45 s late).
// Quad picture: a GPS rescue that plans its braking from the speed it really
// has, not the speed it asked for.
float spinUpdate(float dt, float yawRate) {
  if (spinStarts != drive.spinSeen) {             // a new /spin
    drive.spinSeen    = spinStarts;
    drive.spinOn      = true;
    drive.spinBraking = false;
    drive.spinSettle  = -1;
    drive.spinLeft    = spinDegCmd;
    drive.spinTurn    = spinDegCmd > 0 ? spinRateCmd : -spinRateCmd;
  }
  uint32_t until = spinUntilMs;
  bool live = until && (int32_t)(until - millis()) > 0;
  if (drive.spinOn && !live) drive.spinOn = false;   // cancelled or timed out: the turn ramps to 0 like any stop
  if (!drive.spinOn) { spinLeftNow = 0; return 0; }

  drive.spinLeft -= yawRate * dt;                 // the gyro says how far it really turned
  float toGo = drive.spinTurn > 0 ? drive.spinLeft : -drive.spinLeft;   // deg still to go, + = short of it
  float stopDeg = fabsf(yawRate) * (fabsf(drive.turn) / DRIVE_TURN_ACCEL) / 2;
  if (toGo <= stopDeg) drive.spinBraking = true;  // latched: once braking, it never speeds up again
  float turn = drive.spinBraking ? 0 : drive.spinTurn;

  // Settle (firmware 3.11, config.h): the turn target has ramped down, but the
  // body may still be turning. Steer back by the gyro until it's close and still.
  if (drive.spinBraking && drive.turn == 0 && drive.spinSettle < 0) drive.spinSettle = SPIN_SETTLE_S;   // just landed
  if (drive.spinSettle >= 0) {
    drive.spinSettle -= dt;
    bool still = fabsf(drive.spinLeft) < SPIN_SETTLE_DEG && fabsf(yawRate) < SPIN_SETTLE_DPS;
    if (drive.spinSettle <= 0 || still) drive.spinOn = false;          // done (or out of time)
    else if (fabsf(drive.spinLeft) >= SPIN_SETTLE_DEG)                // + = still short to the right
      turn = constrain(SPIN_SETTLE_GAIN * drive.spinLeft, -SPIN_SETTLE_MAX_TURN, SPIN_SETTLE_MAX_TURN);
  }
  spinLeftNow = drive.spinOn ? drive.spinLeft : 0;
  return drive.spinOn ? turn : 0;
}

// Move x toward target by at most step.
float rampToward(float x, float target, float step) {
  if (x < target) return min(x + step, target);
  if (x > target) return max(x - step, target);
  return x;
}

// The fastest speed target BB1 allows this tick, turns/s (fast drive step 2).
// Two guards, both there to keep PWM for balancing:
//
// 1. Headroom: the wheels get dff x speed of feedforward PWM before the angle
//    loop adds anything. That part only holds the speed; balancing has to fit
//    in what's left under the cap. So the target never asks for more than
//    (cap - DRIVE_PWM_RESERVE) / dff. With dff 25, cap 200, reserve 80 that is
//    4.8 turns/s, above DRIVE_MAX_SPEED: it only bites if someone raises dff
//    or lowers cap. Quad picture: Betaflight's throttle headroom. Full
//    throttle with nothing left over means the PID can't level the quad.
//
// 2. "Too fast, back off": if the motor PWM still sits at the cap for more
//    than DRIVE_SAT_BACKOFF_S while driving (a slope, carpet, a weak pack),
//    the ceiling comes down at DRIVE_STOP_ACCEL_MIN until it stops hitting
//    the cap, then climbs back at dacc. Why: a balance loop at the cap can't
//    push the wheels any further under the body, so if BB1 starts to tip
//    forward there is nothing left to catch it with. Slowing down frees PWM.
//
// saturated: last tick's motor PWM hit the cap (balance.outSaturated, control.h).
float speedCeiling(float dt, bool saturated) {
  float headroom = gainDriveFF > 0 ? max(0.0f, pwmCap - DRIVE_PWM_RESERVE) / gainDriveFF : DRIVE_MAX_SPEED;
  float maxSpeed = min(DRIVE_MAX_SPEED, headroom);

  drive.satSeconds = saturated && drive.speed != 0 ? drive.satSeconds + dt : 0;
  if (drive.satSeconds > DRIVE_SAT_BACKOFF_S)
    drive.ceiling = max(0.0f, min(drive.ceiling, fabsf(drive.speed)) - DRIVE_STOP_ACCEL_MIN * dt);   // back off
  else
    drive.ceiling = min(maxSpeed, drive.ceiling + driveAccelRate * dt);                            // recover at dacc
  return min(maxSpeed, drive.ceiling);
}

// One tick while armed. posNow / diffNow: where the wheels really are (turns
// since arming, same as the speed loop and heading hold measure them).
// saturated: the motor PWM hit the cap last tick (for speedCeiling()).
// yawRate: deg/s from the gyro, right + (for exact-angle spins).
void driveUpdate(float dt, float posNow, float diffNow, bool saturated, float yawRate) {
  uint32_t until = driveUntilMs;
  bool live = until && (int32_t)(until - millis()) > 0;
  float spinTurn = spinUpdate(dt, yawRate);
  drive.wantSpeed = live ? driveSpeedCmd : 0;
  drive.wantTurn  = live ? driveTurnCmd  : spinTurn;   // a /drive cancels the spin (spinUntilMs = 0), so only one is ever set

  bool wasDriving = drive.speed != 0, wasTurning = drive.turn != 0;
  float speedBefore = drive.speed;
  // The command as logged (wantSpeed), but aimed at no faster than the guards allow.
  float top = speedCeiling(dt, saturated);
  float target = constrain(drive.wantSpeed, -top, top);
  // Ramp rate: dacc, except that slowing down (toward 0, or reversing) is
  // never slower than DRIVE_STOP_ACCEL_MIN. Why: a command that runs out
  // (Wi-Fi drop) or /drive/stop at 3 turns/s with dacc 1 would roll on for
  // 3 s and about 0.8 m. At 3 turns/s^2 it is 1 s and about 0.27 m. aff
  // leans BB1 back for the braking, so a faster stop stays balanced.
  bool slowing = fabsf(target) < fabsf(drive.speed) || target * drive.speed < 0;
  float rate = slowing ? max((float)driveAccelRate, DRIVE_STOP_ACCEL_MIN) : (float)driveAccelRate;
  drive.speed = rampToward(drive.speed, target, rate * dt);
  drive.accel = dt > 0 ? (drive.speed - speedBefore) / dt : 0;   // + = the target speeds up forward (or slows a reverse)
  drive.turn  = rampToward(drive.turn,  drive.wantTurn,  DRIVE_TURN_ACCEL * dt);
  bool moving = drive.speed != 0 || drive.turn != 0;
  drive.stopping = !live && !drive.spinOn && moving;   // a spin ramping down to its angle is landing, not stopping

  // Position: while moving, the hold point rides with the wheels, so the
  // speed I term (position) never builds up a "you're behind" debt. Speed P
  // and the feedforward (dff) do the driving.
  // Heading: the target slides at the turn rate (right turn = left wheel
  // ahead = right-minus-left goes down), never more than DRIVE_MAX_LAG from
  // the wheels.
  // Why (floor test 1, 2026-10-25): with a full turn of lag allowed, BB1 kept
  // rolling 2-3 wheel turns and swinging 35-90 deg after you let go, catching
  // up targets that had run ahead.
  // Each axis only lets go of its own hold: turning on the spot keeps the
  // position hold (it rolled back 1-2 wheel turns per turn test without it,
  // 2026-10-26), and driving straight keeps the full heading hold (the lag
  // clamp capped it at th x 0.25 = 5 PWM, under the ~7 PWM the faster right
  // wheel needs).
  // smode 1 (config.h): the target position runs at the asked speed instead,
  // kept within DRIVE_POS_MAX_LAG of the wheels, so wsi works as a speed I
  // while driving without building a catch-up debt.
  if (drive.speed != 0 && speedMode >= 0.5f)
    drive.pos = constrain(drive.pos + drive.speed * dt, posNow - DRIVE_POS_MAX_LAG, posNow + DRIVE_POS_MAX_LAG);
  else if (drive.speed != 0) drive.pos = posNow;
  if (drive.turn != 0) {
    drive.diff -= drive.turn * dt;
    drive.diff  = constrain(drive.diff, diffNow - DRIVE_MAX_LAG, diffNow + DRIVE_MAX_LAG);
  }
  // Just stopped driving: hold right here. Just stopped turning: face the
  // way it faces now. (Per axis, like above.)
  if (wasDriving && drive.speed == 0 && speedMode < 0.5f) drive.pos = posNow;   // smode 1 keeps the target: stops where it was asked to
  if (wasTurning && drive.turn == 0)  drive.diff = diffNow;
  // Just came to a full stop (both ramps at 0): start the I-term relax (control.h).
  // Driving again cancels it: the I term is needed to hold the new speed.
  if ((wasDriving || wasTurning) && !moving) drive.relaxLeft = 2 * gainIRelax;
  else if (moving)                           drive.relaxLeft = 0;

  // Acceleration feedforward (aff): the lean this acceleration needs, smoothed
  // over ACCEL_FF_SMOOTH_S so it rises in a few ticks instead of one.
  float accelLeanWant = gainAccelFF * drive.accel;
  drive.accelLean += (accelLeanWant - drive.accelLean) * min(1.0f, dt / ACCEL_FF_SMOOTH_S);

  // Lean limit: lmax while a speed target is set or ramping down (drive.speed
  // != 0), SPEED_SHIFT_MAX standing. Going up is instant: a bigger limit
  // only allows more, it never moves the setpoint. Coming back down eases at
  // LEAN_LIMIT_EASE_DPS. Why not snap: right after a stop the shift can sit
  // at the limit (braking hard, leaning back 10 deg). Snapping the clamp from
  // 10 to 6 would move the setpoint 4 deg in one tick: a 72 PWM kick at kp 18,
  // twice the 2 deg step test, right while it is braking. Easing turns that
  // into a 0.4 s slope the angle loop can follow (like the drive ramp itself).
  float limitWant = drive.speed != 0 ? max((float)driveLeanMax, SPEED_SHIFT_MAX) : SPEED_SHIFT_MAX;
  drive.leanLimit = limitWant >= drive.leanLimit ? limitWant : max(limitWant, drive.leanLimit - LEAN_LIMIT_EASE_DPS * dt);

  driveSpeedNow = drive.speed;
  driveTurnNow  = drive.turn;
}
