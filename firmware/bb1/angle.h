// angle.h - turn accelerometer + gyro readings into one steady pitch angle.
//
// Two sensors, two weaknesses:
//   accelerometer: knows which way is down, so it never drifts, but every bump
//                  and every wheel push adds fake "tilt". Noisy.
//   gyro:          measures how fast we rotate. Add it up over time and you get
//                  a smooth angle, but any tiny error adds up too. Drifts.
//
// The complementary filter blends them each tick:
//   pitch = alpha * (pitch + gyroRate * dt)  +  (1 - alpha) * accelPitch
//            \____ trust the gyro short term ___/   \__ accel fixes drift slowly __/
//
// Further reading: Shane Colton, "The Balance Filter" (see docs/PID_TUNING.md).

#pragma once
#include "config.h"
#include "imu.h"

struct Angle {
  float pitch      = 0;   // deg, forward lean positive (the filter's answer)
  float pitchRate  = 0;   // deg/s, forward positive (straight from the gyro)
  float accelPitch = 0;   // deg, accelerometer only (for comparison and logs)
  float alpha      = FILTER_ALPHA;
  bool  started    = false;
};

// Tilt from gravity alone. atan2 gives the angle of the (ay, az) vector:
// level = 0, tipping about the chip's X axis moves gravity from z into y.
float accelPitchDeg(const ImuSample &s) {
  return ACCEL_PITCH_SIGN * atan2(s.ay, s.az) * 180.0 / PI;
}

// Call once per control tick with the newest reading and the time since the last tick.
void angleUpdate(Angle &a, const ImuSample &s, float dt) {
  a.accelPitch = accelPitchDeg(s);
  a.pitchRate  = GYRO_PITCH_SIGN * s.gx;   // pitch is the rotation about the chip's X axis

  if (!a.started) {
    // First tick: start from the accelerometer's answer instead of 0,
    // so we don't watch the filter slowly crawl to the right angle.
    a.pitch = a.accelPitch;
    a.started = true;
    return;
  }
  a.pitch = a.alpha * (a.pitch + a.pitchRate * dt) + (1.0 - a.alpha) * a.accelPitch;
}
