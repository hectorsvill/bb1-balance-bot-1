// telemetry.h - the log record and the ring buffer that holds the last few seconds.
//
// Every control tick (200 per second), core 1 fills one LogRecord and pushes it
// into a ring buffer: a fixed array where the newest record overwrites the oldest.
// Core 0 reads records out of it to print them, and later to send them to the
// web page or tools/logger.py.
//
// Why a record per tick: these logs are meant to train a model later, so we keep
// everything the robot sensed (raw, unfiltered), what it estimated, and (from
// step 5 on) what it was told to do and what it did. Add new fields at the END
// of LogRecord and to csvHeader() / printCsv(), and document them in
// docs/CALIBRATION_AND_LOGS.md.

#pragma once
#include <pico/mutex.h>
#include "config.h"

// What the robot is doing. Stored in every record.
enum Mode : uint8_t {
  MODE_BOOT        = 0,
  MODE_CALIBRATING = 1,
  MODE_RUNNING     = 2,   // reading sensors, motors off (until step 6 adds DISARMED / ARMED)
  MODE_MOTOR_TEST  = 3,   // a timed motor test is running (step 5)
  MODE_ARMED       = 4,   // balancing: the PID drives the motors (step 6)
  MODE_FALLEN      = 5,   // fell over while armed; motors off until you arm or disarm again
};

// Bits in LogRecord::flags
const uint8_t FLAG_IMU_ERROR = 1 << 0;   // the IMU read failed this tick (sensor fields repeat the last good values)
const uint8_t FLAG_OVERRUN   = 1 << 1;   // the previous tick's work took longer than 5 ms
const uint8_t FLAG_NO_BIAS   = 1 << 2;   // no successful gyro calibration yet: using GYRO_BIAS_TYPICAL (config.h)
const uint8_t FLAG_SATURATED = 1 << 3;   // armed and the PID output hit the PWM cap

// One control tick. 72 bytes; 2000 of them (10 s) use 144 KB of the 264 KB RAM.
// (Step 5 added 16 bytes: encoders, wheel speeds, motor commands. Step 6 added
// 12: the controller. Its terms are stored x10 as whole numbers, 2 bytes each
// instead of 4 for a float, to keep the 10 s buffer within RAM.)
struct LogRecord {
  uint32_t tick;          // counts up by 1 every tick since boot; a gap means lost records
  uint32_t t_us;          // micros() when the tick started
  uint16_t work_us;       // how long this tick's work took
  uint8_t  mode;          // Mode
  uint8_t  flags;         // FLAG_* bits
  int16_t  raw[7];        // IMU exactly as read: ax, ay, az, temp, gx, gy, gz (gyro NOT bias-corrected)
  float    pitch;         // deg, filtered, forward +
  float    pitch_rate;    // deg/s, bias-corrected, forward +
  float    accel_pitch;   // deg, accelerometer only
  // step 5: wheels. Forward is positive for all of these.
  int32_t  enc_l;         // left encoder counts since boot
  int32_t  enc_r;         // right encoder counts since boot
  int16_t  speed_l;       // left wheel speed, counts/s (over the last 50 ms)
  int16_t  speed_r;       // right wheel speed, counts/s
  int16_t  pwm_l;         // PWM sent to the left motor this tick, -255..255 (the "action")
  int16_t  pwm_r;         // PWM sent to the right motor
  // step 6: the balance controller (0 when disarmed)
  int16_t  setpoint_cdeg; // balance trim, hundredths of a degree
  int16_t  p_x10;         // P term in PWM, x10 (so 123 = 12.3 PWM)
  int16_t  i_x10;         // I term
  int16_t  d_x10;         // D term
  uint16_t gains_ver;     // which gains were in use (counts up on every change)
  uint8_t  reason;        // why it last disarmed or refused to arm (Reason in control.h; 0 = none).
                          // Fits in what used to be padding, so the record stays 68 bytes.
  uint8_t  web_age;       // time since the page last asked for /state, in 10 ms steps (255 = 2.55 s or more).
                          // Shows Wi-Fi stalls; the last padding byte.
  // 2026-10-24: battery (log format 6). Makes the record 72 bytes (2 bytes of padding left).
  uint16_t batt_mv;       // battery voltage in mV (battery.h; 0 = no wire / switch off)
  // 2026-10-24: auto-arm (log format 7). Uses 1 of the 2 padding bytes: still 72 bytes.
  uint8_t  auto_state;    // AutoState in auto_arm.h: why it is or isn't starting by itself
};

// ---------- the ring buffer ----------
//
//   slot:    0    1    2    3   ...  1999
//          [ r2000 r2001 r1998 r1999 ... ]   <- after 2001 records, the oldest are overwritten
//
// totalWritten counts every record ever pushed. The newest record is in slot
// (totalWritten - 1) % size, and record number n (its tick) lives in slot n % size.
//
// The mutex: core 1 writes a slot while core 0 might be copying the same slot.
// Without a lock, core 0 could copy half an old record and half a new one.
// Each side holds the lock only while copying one 40-byte record (about a
// microsecond), so core 1 never waits long.

LogRecord logBuffer[LOG_BUFFER_RECORDS];
volatile uint32_t totalWritten = 0;
auto_init_mutex(logMutex);

// Core 1: add the newest record.
void logPush(const LogRecord &r) {
  mutex_enter_blocking(&logMutex);
  logBuffer[totalWritten % LOG_BUFFER_RECORDS] = r;
  totalWritten = totalWritten + 1;
  mutex_exit(&logMutex);
}

// Core 0: copy record number n out. Returns false if it's already been
// overwritten (too old) or doesn't exist yet.
bool logGet(uint32_t n, LogRecord &out) {
  bool ok = false;
  mutex_enter_blocking(&logMutex);
  uint32_t total = totalWritten;
  if (n < total && total - n <= LOG_BUFFER_RECORDS) {
    out = logBuffer[n % LOG_BUFFER_RECORDS];
    ok = true;
  }
  mutex_exit(&logMutex);
  return ok;
}

// The number of the oldest record still in the buffer.
uint32_t logOldest() {
  uint32_t total = totalWritten;
  return total > LOG_BUFFER_RECORDS ? total - LOG_BUFFER_RECORDS : 0;
}

// ---------- CSV ----------
// One header line and one line per record. The same columns will be used by
// the web page download and tools/logger.py.

const char *CSV_HEADER =
  "tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,pitch_rate,accel_pitch,"
  "enc_l,enc_r,speed_l,speed_r,pwm_l,pwm_r,setpoint,p_term,i_term,d_term,gains_ver,reason,web_age_ms,batt_v,auto_state";

void csvHeader(Print &p) {
  p.println(CSV_HEADER);
}

// Write one record as a CSV line (with "\n") into buf. Returns the length.
int formatCsv(char *buf, size_t size, const LogRecord &r) {
  return snprintf(buf, size, "%lu,%lu,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%ld,%ld,%d,%d,%d,%d,%.2f,%.1f,%.1f,%.1f,%u,%u,%u,%.2f,%u\n",
                  (unsigned long)r.tick, (unsigned long)r.t_us, r.work_us, r.mode, r.flags,
                  r.raw[0], r.raw[1], r.raw[2], r.raw[3], r.raw[4], r.raw[5], r.raw[6],
                  r.pitch, r.pitch_rate, r.accel_pitch,
                  (long)r.enc_l, (long)r.enc_r, r.speed_l, r.speed_r, r.pwm_l, r.pwm_r,
                  r.setpoint_cdeg / 100.0, r.p_x10 / 10.0, r.i_x10 / 10.0, r.d_x10 / 10.0, r.gains_ver, r.reason, r.web_age * 10, r.batt_mv / 1000.0, r.auto_state);
}

void printCsv(Print &p, const LogRecord &r) {
  char buf[240];
  formatCsv(buf, sizeof(buf), r);
  p.print(buf);
}
