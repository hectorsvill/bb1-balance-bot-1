// odom.h - odometry: where BB1 is on the floor since it last armed (firmware 3.12).
//
// Every tick: how far the wheels rolled (both wheels' average, mm) and which
// way the body faces (the gyro heading, deg). Add up the little steps in that
// direction and you get x (forward from where it armed) and y (to the right).
//
//     step = mm rolled this tick
//     x += step x cos(heading)        heading 0 = the way it faced when it armed
//     y += step x sin(heading)        heading 90 = turned right
//
// The heading comes from the gyro, not the wheels: in a spin the wheels scrub
// sideways (the gyro saw 0.83 of what the encoders said, runs 189-190). Errors
// grow with distance (wheel slip, the assumed 520 counts per turn, gyro drift):
// a few cm over a few meters on wood. Quad picture: GPS-less position hold,
// dead reckoning from what you know about your own motion.
// Only while armed: lying down or carried, nothing is counted.

#pragma once
#include <math.h>
#include "config.h"
#include "shared.h"

struct Odom {
  float x = 0, y = 0;      // mm since arming: x forward, y right
  float lastTurns = 0;     // wheel turns since arming at the last tick
  float yawAtArm = 0;      // gyro heading when it armed (deg)
  float heading = 0;       // deg since arming, right +
};

Odom odom;                 // only core 1 touches this

// On every arm (the tick the encoders are zeroed): start from (0, 0), heading 0.
void odomReset(float yawNow) {
  odom = Odom();
  odom.yawAtArm = yawNow;
  odomXNow = odomYNow = odomHeadingNow = 0;
}

// Every armed tick. turnsNow: wheel turns since arming (average of both wheels).
// yawNow: the gyro heading (angle.h yaw, deg, right +).
void odomUpdate(float turnsNow, float yawNow) {
  float step = (turnsNow - odom.lastTurns) * MM_PER_TURN;
  odom.lastTurns = turnsNow;
  odom.heading = yawNow - odom.yawAtArm;
  float h = odom.heading * (float)(PI / 180.0);
  odom.x += step * cosf(h);
  odom.y += step * sinf(h);
  odomXNow = odom.x;
  odomYNow = odom.y;
  odomHeadingNow = odom.heading;
}
