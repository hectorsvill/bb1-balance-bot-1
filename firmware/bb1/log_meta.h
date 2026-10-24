// log_meta.h - the "# setting,value" lines at the top of every log file.
//
// A log file should explain itself months later (and to a model trained on it):
// which firmware made it, the gyro bias that was subtracted, the filter weight,
// and how to turn raw counts into units. Every way of saving a log uses this
// same block: the serial "dump", the web page's Download, and tools/logger.py.

#pragma once
#include "config.h"
#include "imu.h"
#include "shared.h"

// Bump this when the log columns change, so old and new files can be told apart.
const char *FIRMWARE_NAME = "bb1 step 8";
const int   LOG_FORMAT    = 6;   // 2: step 5 added enc_l..pwm_r; 3: step 6 added setpoint..gains_ver; 4: reason; 5: web_age_ms; 6: batt_v

String logMetaLines() {
  String m;
  m += "# firmware," + String(FIRMWARE_NAME) + "\n";
  m += "# log_format," + String(LOG_FORMAT) + "\n";
  m += "# gyro_bias_dps," + String(gyroBias[0], 3) + "," + String(gyroBias[1], 3) + "," + String(gyroBias[2], 3) + "\n";
  m += "# alpha," + String(requestedAlpha, 3) + ",loop_hz," + String(LOOP_HZ) +
       ",accel_lsb_per_g," + String(ACCEL_PER_G, 1) + ",gyro_lsb_per_dps," + String(GYRO_PER_DPS, 1) + "\n";
  m += "# accel_pitch_sign," + String((int)ACCEL_PITCH_SIGN) + ",gyro_pitch_sign," + String((int)GYRO_PITCH_SIGN) + "\n";
  m += "# counts_per_rev," + String(COUNTS_PER_REV) + ",speed_window_ticks," + String(SPEED_WINDOW) +
       ",pwm_max," + String(PWM_MAX) + ",pwm_freq_hz," + String(PWM_FREQ) + "\n";
  m += "# battery_divider," + String(BATTERY_DIVIDER, 2) + ",battery_pin,GP" + String(BATTERY_PIN) + ",battery_cells," + String(BATTERY_CELLS) + "\n";
  m += "# gains_ver," + String(gainsVersion) + ",kp," + String(gainP, 3) + ",ki," + String(gainI, 3) +
       ",kd," + String(gainD, 3) + ",trim," + String(balanceTrim, 2) + ",pwm_cap," + String(pwmCap, 0) +
       ",deadband," + String(motorDeadband, 0) + ",speed_p," + String(gainSpeedP, 2) + ",speed_i," + String(gainSpeedI, 2) + ",turn_hold," + String(gainTurnHold, 1) + ",auto_arm," + String((int)autoArm) + "\n";
  return m;
}
