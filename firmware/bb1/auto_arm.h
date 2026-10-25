// auto_arm.h - power-on and auto-arm: when BB1 starts balancing by itself.
//
// Switch BB1 on standing up and it balances. Switch it on lying down and it
// waits until you stand it up and hold it still for AUTO_ARM_HOLD_MS. Every
// edge case (held in the air, falls, STOP, battery...) is in docs/POWER_ON.md.
//
// It's a small state machine. Each tick autoArmUpdate() looks at the robot and
// picks one state; the state goes in the log (auto_state column), on serial
// and on the pages, so you can always see why it is or isn't arming.
//
//   OFF_SETTING ---- "auto 1" ----+
//                                  v
//              +-----------------> WAIT_UPRIGHT  (lying down, leaning, moving, battery low)
//              |                       |  upright and still
//              |                       v
//              |  fell / lifted     HOLDING  (counting to AUTO_ARM_HOLD_MS)
//              |                       |  held long enough -> arm
//              +---------------- BALANCING
//                                      |  STOP, timer, Wi-Fi, IMU...      too many falls
//                                      v                                       v
//                                 OFF_STOPPED                              OFF_FALLS
//                                 (both wait for ARM or a power-up)
//
// Like a quad's arming rules in Betaflight: it only arms when it's safe (level,
// still), and a disarm switch (STOP) always wins. (NO_IMU: the IMU never answered.)
//
// Runs on core 1, once per tick. Core 0 only reads autoArmState (shared.h).

#pragma once
#include "config.h"
#include "shared.h"
#include "angle.h"
#include "battery.h"
#include "control.h"

enum AutoState : uint8_t {
  AUTO_OFF_SETTING  = 0,  // "auto 0": only ARM starts balancing
  AUTO_OFF_STOPPED  = 1,  // STOP (or another stop that wasn't a fall): waits for ARM
  AUTO_OFF_FALLS    = 2,  // fell AUTO_MAX_FALLS times in a row quickly: waits for ARM
  AUTO_NO_IMU       = 3,  // the IMU never answered at startup: can't balance
  AUTO_WAIT_UPRIGHT = 4,  // lying down, leaning, moving, battery low or a motor test
  AUTO_HOLDING      = 5,  // upright and still: counting
  AUTO_BALANCING    = 6,  // armed (by itself or by ARM)
};

const char *autoStateText(uint8_t s) {
  switch (s) {
    case AUTO_OFF_SETTING:  return "off (auto 0): press ARM";
    case AUTO_OFF_STOPPED:  return "off: stopped, press ARM";
    case AUTO_OFF_FALLS:    return "off: fell too often, press ARM";
    case AUTO_NO_IMU:       return "off: IMU not found";
    case AUTO_WAIT_UPRIGHT: return "waiting: stand it up and hold still";
    case AUTO_HOLDING:      return "holding: starts in a moment";
    case AUTO_BALANCING:    return "balancing";
    default:                return "";
  }
}

struct AutoArm {
  AutoState state = AUTO_WAIT_UPRIGHT;   // at power-up: on, waiting
  unsigned long holdStartMs = 0;      // when HOLDING began
  bool wasArmed = false;              // armed last tick (to notice a disarm)
  unsigned long fallMs[AUTO_MAX_FALLS] = {};   // when the last few falls happened
  int fallCount = 0;                  // falls stored in fallMs (up to AUTO_MAX_FALLS)
};

AutoArm autoArmer;                    // only core 1 touches this

// You pressed STOP (page, space bar, Enter, serial "disarm"), armed or not.
// STOP always wins: no auto-arm until you press ARM or power-cycle.
void autoArmStop() {
  autoArmer.state = AUTO_OFF_STOPPED;
}

// You pressed ARM: you're in charge again, so forget old falls and let
// auto-arm catch the next fall.
void autoArmOnManualArm() {
  autoArmer.fallCount = 0;
  autoArmer.state = AUTO_WAIT_UPRIGHT;   // the next update sees it armed -> BALANCING
}

// A fall (or a lift) just happened. Too many in a short time means something is
// wrong (trim, battery, a loose wire): stop trying until you look at it.
bool tooManyFalls(unsigned long now) {
  AutoArm &aa = autoArmer;
  if (aa.fallCount < AUTO_MAX_FALLS) aa.fallCount++;
  for (int n = aa.fallCount - 1; n > 0; n--) aa.fallMs[n] = aa.fallMs[n - 1];   // newest first
  aa.fallMs[0] = now;
  return aa.fallCount == AUTO_MAX_FALLS && now - aa.fallMs[AUTO_MAX_FALLS - 1] < AUTO_FALL_WINDOW_MS;
}

bool uprightAndStill(const Angle &a) {
  return fabs(a.pitch - balanceTrim) < AUTO_ARM_ANGLE && fabs(a.pitchRate) < AUTO_ARM_RATE;
}

// Call every tick, before balanceTick(). Returns true when it's time to arm.
bool autoArmUpdate(const Angle &a, bool imuOk) {
  AutoArm &aa = autoArmer;
  unsigned long now = millis();

  // 1. Did balancing just stop? A fall or a lift: wait to be stood up again.
  //    Anything else (STOP, the timer, Wi-Fi, IMU errors, low battery): stay off
  //    until ARM. A sagging pack recovers the moment the motors stop, so this
  //    must not count as a fall or it would arm again and sag straight back down.
  if (aa.wasArmed && !balance.armed) {
    bool fellOrLifted = disarmReason == REASON_FELL || disarmReason == REASON_LIFTED;
    if (!fellOrLifted)          aa.state = AUTO_OFF_STOPPED;
    else if (tooManyFalls(now)) aa.state = AUTO_OFF_FALLS;
    else                        aa.state = AUTO_WAIT_UPRIGHT;
  }
  aa.wasArmed = balance.armed;

  // 2. Pick this tick's state, first match wins.
  bool offUntilArm = aa.state == AUTO_OFF_STOPPED || aa.state == AUTO_OFF_FALLS;
  if (balance.armed)                    aa.state = AUTO_BALANCING;
  else if (offUntilArm)                 { /* stay off */ }
  else if (autoArm < 0.5f)              aa.state = AUTO_OFF_SETTING;
  else if (!imuFound)                   aa.state = AUTO_NO_IMU;
  else if (!imuOk || motorTestRunning || batteryTooLow() || !uprightAndStill(a))
                                        aa.state = AUTO_WAIT_UPRIGHT;
  else if (aa.state != AUTO_HOLDING)    { aa.state = AUTO_HOLDING; aa.holdStartMs = now; }

  autoArmState = aa.state;             // for core 0: serial and the pages

  // 3. Held upright and still long enough: arm.
  return aa.state == AUTO_HOLDING && now - aa.holdStartMs >= AUTO_ARM_HOLD_MS;
}
