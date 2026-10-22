// pid.h - a small PID controller for the balance loop.
//
// Follows Brett Beauregard's "Improving the Beginner's PID" (docs/PID_TUNING.md):
//   - fixed sample time: called once per 5 ms tick, so dt is a constant
//   - derivative on the measurement: we pass in the gyro rate directly instead
//     of differentiating the error, so changing the setpoint never causes a kick
//     and we avoid the noise of subtracting two angles
//   - anti-windup: the I term is clamped, and stops growing while the output is
//     already at its limit (otherwise it keeps "charging up" and overshoots later)
//
// If you fly FPV: this is the same PID as one axis of Betaflight's pitch loop.
//   gainP (Kp) = Betaflight P: how hard it snaps back toward level. Too high =
//                fast oscillation (like P wobble on a quad).
//   gainI (Ki) = Betaflight I: holds the angle against something steady (a
//                heavy side, wind on a quad). Too high = slow wobble. Start at 0.
//   gainD (Kd) = Betaflight D: brakes the motion, damps the bounce-back. Too
//                high = hot, buzzing motors (D amplifies gyro noise).
// Differences from a quad: one axis, no stick input (the setpoint is "stand
// upright", the trim), and the wheels are the motors. The usual names Kp, Ki,
// Kd ("K" = gain) are still what the serial commands, page and logs call them.
//
// Sign convention (BB1): error = pitch - setpoint, so a forward lean gives a
// positive error, and positive output drives the wheels forward, back under the
// robot. All three gains are positive numbers.

#pragma once

struct Pid {
  float gainP = 0, gainI = 0, gainD = 0;   // Kp, Ki, Kd: the numbers you tune
  float limit = 255;        // output is clamped to -limit..+limit (the PWM cap)
  float integral = 0;       // sum of error * dt, in deg * s

  // The three parts of the last output, kept for the log.
  float p = 0, i = 0, d = 0;
  bool  saturated = false;  // the output hit the limit this tick

  // error: deg (forward +); rate: deg/s (forward +); dt: seconds.
  float update(float error, float rate, float dt) {
    p = gainP * error;
    d = gainD * rate;
    float out = p + gainI * integral + d;
    saturated = fabs(out) >= limit;
    // Only let the integral grow while we're not already at the limit.
    if (!saturated && gainI > 0) {
      integral += error * dt;
      integral = constrain(integral, -limit / gainI, limit / gainI);   // I term never beyond the cap on its own
    }
    i = gainI * integral;
    out = constrain(p + i + d, -limit, limit);
    return out;
  }

  void reset() {
    integral = 0;
    p = i = d = 0;
    saturated = false;
  }
};
