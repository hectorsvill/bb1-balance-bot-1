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
  driveSpeedCmd = speed;
  driveTurnCmd  = turn;
  driveUntilMs  = ms ? millis() + ms : 0;
  driveCommands = driveCommands + 1;
  return true;
}

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

  bool wasDriving = drive.speed != 0, wasTurning = drive.turn != 0;
  drive.speed = rampToward(drive.speed, drive.wantSpeed, DRIVE_ACCEL * dt);
  drive.turn  = rampToward(drive.turn,  drive.wantTurn,  DRIVE_TURN_ACCEL * dt);
  bool moving = drive.speed != 0 || drive.turn != 0;
  drive.stopping = !live && moving;

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
  if (drive.speed != 0) drive.pos = posNow;
  if (drive.turn != 0) {
    drive.diff -= drive.turn * dt;
    drive.diff  = constrain(drive.diff, diffNow - DRIVE_MAX_LAG, diffNow + DRIVE_MAX_LAG);
  }
  // Just stopped driving: hold right here. Just stopped turning: face the
  // way it faces now. (Per axis, like above.)
  if (wasDriving && drive.speed == 0) drive.pos  = posNow;
  if (wasTurning && drive.turn == 0)  drive.diff = diffNow;

  driveSpeedNow = drive.speed;
  driveTurnNow  = drive.turn;
}
