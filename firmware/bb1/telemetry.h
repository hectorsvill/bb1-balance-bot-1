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
};

// Bits in LogRecord::flags
const uint8_t FLAG_IMU_ERROR = 1 << 0;   // the IMU read failed this tick (sensor fields repeat the last good values)
const uint8_t FLAG_OVERRUN   = 1 << 1;   // the previous tick's work took longer than 5 ms

// One control tick. 40 bytes; 2000 of them (10 s) use 80 KB of the 264 KB RAM.
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

void csvHeader(Print &p) {
  p.println("tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,pitch_rate,accel_pitch");
}

void printCsv(Print &p, const LogRecord &r) {
  char buf[160];
  snprintf(buf, sizeof(buf), "%lu,%lu,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f",
           (unsigned long)r.tick, (unsigned long)r.t_us, r.work_us, r.mode, r.flags,
           r.raw[0], r.raw[1], r.raw[2], r.raw[3], r.raw[4], r.raw[5], r.raw[6],
           r.pitch, r.pitch_rate, r.accel_pitch);
  p.println(buf);
}
