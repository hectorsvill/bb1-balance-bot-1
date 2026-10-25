// drive.h - driving from the page or the API (milestone 2).
//
// BB1 doesn't drive by sending PWM to the wheels: the balance loop owns them.
// Instead it moves the targets of the two outer loops in control.h:
//
//   speed loop (sp/si)  holds the wheels at a speed and a position.
//                       Standing still both targets are 0. Driving, the speed
//                       target is the asked speed and the position target
//                       rolls forward at that speed, so the speed I term
//                       doesn't pull BB1 back to where it armed.
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
// Targets ramp (DRIVE_ACCEL) so a full-stick jab can't kick it over.
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
};

Drive drive;            // only core 1 touches this

// On every arm: stand still where it is.
void driveReset() {
  drive = Drive();
  driveSpeedNow = driveTurnNow = 0;
}

// Move x toward target by at most step.
float rampToward(float x, float target, float step) {
  if (x < target) return min(x + step, target);
  if (x > target) return max(x - step, target);
  return x;
}

// One tick while armed. posNow / diffNow: where the wheels really are (turns
// since arming, same as the speed loop and heading hold measure them).
void driveUpdate(float dt, float posNow, float diffNow) {
  uint32_t until = driveUntilMs;
  bool live = until && (int32_t)(until - millis()) > 0;
  drive.wantSpeed = live ? driveSpeedCmd : 0;
  drive.wantTurn  = live ? driveTurnCmd  : 0;

  drive.speed = rampToward(drive.speed, drive.wantSpeed, DRIVE_ACCEL * dt);
  drive.turn  = rampToward(drive.turn,  drive.wantTurn,  DRIVE_TURN_ACCEL * dt);
  drive.stopping = !live && (drive.speed != 0 || drive.turn != 0);

  // Roll the targets along. Right turn = left wheel gets ahead = the
  // right-minus-left difference goes down.
  drive.pos  += drive.speed * dt;
  drive.diff -= drive.turn * dt;

  // While moving, never let a target run more than DRIVE_MAX_LAG ahead of the
  // wheels (stuck against a wall, then let go = no sudden lurch). Standing
  // still it's left alone, so a push still rolls back home as before.
  if (drive.speed != 0) drive.pos  = constrain(drive.pos,  posNow - DRIVE_MAX_LAG,  posNow + DRIVE_MAX_LAG);
  if (drive.turn  != 0) drive.diff = constrain(drive.diff, diffNow - DRIVE_MAX_LAG, diffNow + DRIVE_MAX_LAG);

  driveSpeedNow = drive.speed;
  driveTurnNow  = drive.turn;
}
