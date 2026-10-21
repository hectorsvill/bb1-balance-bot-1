// bb1.ino - BB1 balance bot main firmware.
//
// Step 4 of docs/FIRMWARE_PLAN.md: saving logs from the web page (on top of step 3's Wi-Fi page).
//
//   core 1 (setup1 / loop1): the 200 Hz control loop and nothing else.
//       read IMU -> complementary filter -> fill a LogRecord -> push it into the ring buffer
//   core 0 (setup / loop):   everything slow.
//       Wi-Fi + web page, serial commands, the 10 Hz readout, dumping the log buffer
//
// Why split them: printing, and later Wi-Fi, can take milliseconds at random
// moments. On its own core the control loop keeps its 5 ms beat no matter what.
//
// Motors are held OFF (STBY low) the whole time. Nothing moves in this step.
//
// Board: Raspberry Pi Pico W, arduino-pico core by Earle Philhower (6.3.0).
// Serial: 115200 baud, line ending "Newline". Type "help" for commands.
// Power rule (no 1N5819 yet): D153C on before USB; USB out before D153C off.
//
// Files:
//   config.h     pins, signs, rates, buffer size
//   imu.h        MPU-6050 driver + gyro bias calibration
//   angle.h      complementary filter
//   telemetry.h  LogRecord, the ring buffer, CSV output
//   shared.h     requests from core 0 to core 1, status back
//   wifi_setup.h own network / home network, saved settings
//   web.h        web server routes;  page.h  the page itself
//   log_meta.h   the "# setting" lines at the top of every log file
//   secrets.h    own-network password (not in git; copy secrets.example.h)

#include "config.h"
#include "imu.h"
#include "angle.h"
#include "telemetry.h"
#include "shared.h"
#include "wifi_setup.h"
#include "log_meta.h"
#include "web.h"

// Give core 1 its own 8 KB stack instead of splitting one 8 KB stack between the
// cores (see the arduino-pico "Multicore" docs). Wi-Fi on core 0 will want room.
bool core1_separate_stack = true;

// =====================================================================
// Core 1: the control loop
// =====================================================================

Angle angle;               // only core 1 touches this; core 0 sees it through the log records

void calibrateNow() {
  lastCalibrationOk = imuCalibrate();
  if (lastCalibrationOk) everCalibrated = true;
  angle.started = false;   // restart the filter from the accelerometer angle
  calibrationsDone = calibrationsDone + 1;
}

void setup1() {
  imuFound = imuBegin();
  delay(500);              // let the chip settle and hands leave the robot
  if (imuFound) calibrateNow();
}

void loop1() {
  // Same fixed-rate loop as step 1: each tick starts exactly 5 ms after the last.
  static unsigned long nextTick = micros();
  static unsigned long lastTick = micros();
  static uint32_t tick = 0;
  static bool overranLastTick = false;
  static ImuSample s;      // static: if a read fails we keep the last good values

  while ((long)(micros() - nextTick) < 0) {}   // wait for the tick
  unsigned long now = micros();
  float dt = (now - lastTick) / 1e6;
  lastTick = now;
  nextTick += LOOP_US;

  // Requests from core 0 are handled here, between ticks, never in the middle of one.
  if (requestCalibrate) {
    calibrateNow();                            // takes 2 s; no records during it
    requestCalibrate = false;
    nextTick = micros() + LOOP_US;
    lastTick = micros();
    return;
  }
  angle.alpha = requestedAlpha;

  // If the startup calibration failed (someone was holding the robot), try
  // again every 3 s until it's set down still. Without a bias the angle drifts.
  static unsigned long lastCalAttemptMs = 0;
  if (imuFound && !everCalibrated && millis() - lastCalAttemptMs > 3000) {
    lastCalAttemptMs = millis();
    requestCalibrate = true;                   // handled at the top of the next tick
  }

  // ---- the work of one tick ----
  LogRecord r;
  r.tick  = tick++;
  r.t_us  = now;
  r.mode  = MODE_RUNNING;
  r.flags = overranLastTick ? FLAG_OVERRUN : 0;
  if (!everCalibrated) r.flags |= FLAG_NO_BIAS;

  if (imuRead(s)) {
    angleUpdate(angle, s, dt);
  } else {
    r.flags |= FLAG_IMU_ERROR;
  }

  for (int i = 0; i < 7; i++) r.raw[i] = s.raw[i];
  r.pitch       = angle.pitch;
  r.pitch_rate  = angle.pitchRate;
  r.accel_pitch = angle.accelPitch;
  // ------------------------------

  unsigned long work = micros() - now;
  r.work_us = (uint16_t)min(work, 65535UL);
  logPush(r);              // counted in the next tick's work, a few microseconds

  overranLastTick = work > LOOP_US;
  if (overranLastTick) nextTick = micros() + LOOP_US;   // fell behind: don't catch up in a burst
}

// =====================================================================
// Core 0: serial commands and the readout
// =====================================================================

bool csvMode = false;      // "csv": the old step-1 comma-separated readout (for summarize_serial.py)
bool quiet = false;        // "quiet": pause the 10 Hz readout

void printHelp() {
  Serial.println();
  Serial.println("BB1 step 4 commands:");
  Serial.println("  cal       measure the gyro bias again (lay the robot still first)");
  Serial.println("  alpha N   set the filter weight, 0.90 to 0.999 (now " + String(requestedAlpha, 3) + ")");
  Serial.println("  csv       switch the readout to comma-separated lines (again to switch back)");
  Serial.println("  dump      print the whole log buffer (about the last " + String(LOG_SECONDS - 1) + " s) as CSV");
  Serial.println("  dump N    print only the last N seconds");
  Serial.println("  wifi      show the network and the page address");
  Serial.println("  wifi reset  forget the home network and restart on the own network");
  Serial.println("  quiet     stop / restart the 10 Hz readout (easier to read other output)");
  Serial.println("  help      this list");
}

void printBias() {
  Serial.print("  bias deg/s  x ");
  Serial.print(gyroBias[0], 2);
  Serial.print("  y ");
  Serial.print(gyroBias[1], 2);
  Serial.print("  z ");
  Serial.println(gyroBias[2], 2);
}

// Print records from the buffer as CSV, oldest first.
// Core 1 keeps writing while we print, so we start 1 s after the oldest record:
// that gives us a second of head start before the oldest ones get overwritten.
void dumpLog(float seconds) {
  uint32_t total = totalWritten;
  uint32_t first = logOldest() + LOOP_HZ;
  if (seconds > 0) {
    uint32_t wanted = (uint32_t)(seconds * LOOP_HZ);
    if (total > wanted && total - wanted > first) first = total - wanted;
  }
  if (first >= total) {
    Serial.println("# nothing in the log yet");
    return;
  }

  // Lines starting with # are settings and notes, not data (see log_meta.h).
  Serial.print(logMetaLines());
  csvHeader(Serial);

  uint32_t skipped = 0;
  LogRecord r;
  for (uint32_t n = first; n < total; n++) {
    if (logGet(n, r)) printCsv(Serial, r);
    else skipped++;
  }
  Serial.println("# end, " + String(total - first - skipped) + " records" +
                 (skipped ? ", " + String(skipped) + " overwritten before they could be printed" : ""));
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;
  Serial.println("> " + cmd);
  if (cmd == "cal") {
    Serial.println("Calibrating gyro: keep the robot still for 2 s...");
    requestCalibrate = true;                   // core 1 does it; we report when it's done
  } else if (cmd.startsWith("alpha")) {
    float a = cmd.substring(5).toFloat();
    if (a >= 0.90 && a <= 0.999) {
      requestedAlpha = a;
      Serial.println("alpha = " + String(a, 3));
    } else {
      Serial.println("alpha must be between 0.90 and 0.999");
    }
  } else if (cmd == "csv") {
    csvMode = !csvMode;
    if (csvMode) Serial.println("ms,pitch,accel_pitch,pitch_rate,loop_avg_us,loop_max_us");
  } else if (cmd == "wifi") {
    wifiPrintStatus();
  } else if (cmd == "wifi reset") {
    wifiForgetHomeNetwork();
    Serial.println("Home network forgotten. Restarting on the own network...");
    delay(200);
    rp2040.reboot();
  } else if (cmd == "quiet") {
    quiet = !quiet;
  } else if (cmd.startsWith("dump")) {
    dumpLog(cmd.substring(4).toFloat());       // "dump" alone gives 0 = everything
  } else {
    printHelp();
  }
}

String line;
void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line = "";
    } else {
      line += c;
    }
  }
}

// The 10 Hz readout. It reads the records written since the last printout
// straight out of the log buffer, so what you see is exactly what gets logged.
void printStatus() {
  static uint32_t nextToRead = 0;
  uint32_t total = totalWritten;
  if (nextToRead < logOldest()) nextToRead = logOldest();

  LogRecord r, newest;
  bool any = false;
  unsigned long workSum = 0, workMax = 0, count = 0;
  static unsigned long overruns = 0, imuErrors = 0;   // totals since boot
  for (; nextToRead < total; nextToRead++) {
    if (!logGet(nextToRead, r)) continue;
    workSum += r.work_us;
    workMax = max(workMax, (unsigned long)r.work_us);
    count++;
    if (r.flags & FLAG_OVERRUN) overruns++;
    if (r.flags & FLAG_IMU_ERROR) imuErrors++;
    newest = r;
    any = true;
  }
  if (!any) return;                            // nothing new (for example during calibration)
  unsigned long avg = workSum / count;

  if (csvMode) {
    Serial.print(newest.t_us / 1000);          Serial.print(',');
    Serial.print(newest.pitch, 2);             Serial.print(',');
    Serial.print(newest.accel_pitch, 2);       Serial.print(',');
    Serial.print(newest.pitch_rate, 2);        Serial.print(',');
    Serial.print(avg);                         Serial.print(',');
    Serial.println(workMax);
  } else {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "pitch %+6.1f | accel only %+6.1f | rate %+7.1f deg/s | loop avg %4lu us max %4lu us | overruns %lu imu errors %lu | tick %lu",
             newest.pitch, newest.accel_pitch, newest.pitch_rate, avg, workMax, overruns, imuErrors,
             (unsigned long)newest.tick);
    Serial.println(buf);
  }
}

void setup() {
  // Motors off before anything else: STBY low puts the D153C to sleep.
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);

  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000) {}   // wait up to 5 s for a serial monitor

  Serial.println();
  Serial.println("BB1 step 4: Wi-Fi page + log download. Motors are off.");
  wifiStart();             // up to 15 s if a home network is saved; core 1 is already running
  webBegin();
  wifiPrintStatus();
  printHelp();
}

void loop() {
  // setup1() on core 1 runs at the same time as setup() here, so we can't know
  // in setup() whether the IMU answered yet. Check once, 3 s after boot.
  static bool imuChecked = false;
  if (!imuChecked && millis() > 3000) {
    imuChecked = true;
    if (!imuFound) Serial.println("! MPU-6050 not found at 0x68: check VCC (pin 36), GND (pin 8), SDA GP4, SCL GP5.");
  }

  // Report each finished calibration (core 1 counts them up).
  static uint32_t calibrationsSeen = 0;
  if (calibrationsDone != calibrationsSeen) {
    calibrationsSeen = calibrationsDone;
    if (lastCalibrationOk) {
      Serial.println("Gyro calibrated.");
      printBias();
    } else {
      Serial.println(everCalibrated
                     ? "! robot moved (or the IMU didn't answer): bias not changed. Type cal to retry."
                     : "! robot moved during calibration. Set it down still; retrying every 3 s.");
    }
  }

  readSerial();
  wifiLoop();
  webLoop();

  static unsigned long lastPrint = 0;
  if (!quiet && millis() - lastPrint >= 1000 / PRINT_HZ) {
    lastPrint = millis();
    printStatus();
  }
}
