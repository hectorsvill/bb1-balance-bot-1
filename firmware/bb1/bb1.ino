// bb1.ino - BB1 balance bot main firmware.
//
// Step 6 of docs/FIRMWARE_PLAN.md: arm / disarm, safety checks and the angle
// PD loop (on top of step 5's motors and step 4's logging page).
//
//   core 1 (setup1 / loop1): the 200 Hz control loop and nothing else.
//       read IMU -> complementary filter -> read encoders -> balance PID or motor test
//       -> LogRecord -> ring buffer
//   core 0 (setup / loop):   everything slow.
//       Wi-Fi + web page, serial commands, the 10 Hz readout, dumping the log buffer
//
// Why split them: printing, and later Wi-Fi, can take milliseconds at random
// moments. On its own core the control loop keeps its 5 ms beat no matter what.
//
// Motors are OFF (STBY low) unless armed (balancing) or during a motor test,
// which always stops by itself (MOTOR_TEST_MAX_MS in config.h). Arming needs
// the robot near upright; falling past 45 deg, IMU errors or losing the page disarm it.
//
// Board: Raspberry Pi Pico W, arduino-pico core by Earle Philhower (6.3.0).
// Serial: 115200 baud, line ending "Newline". Type "help" for commands.
// Power rule (no 1N5819 yet): D153C on before USB; USB out before D153C off.
//
// Files:
//   config.h     pins, signs, rates, buffer size
//   imu.h        MPU-6050 driver + gyro bias calibration
//   angle.h      complementary filter
//   motors.h     D153C motor driver, encoders, wheel speed
//   pid.h        a small PID;  control.h  arm / disarm, safety checks, the angle loop
//   telemetry.h  LogRecord, the ring buffer, CSV output
//   shared.h     requests from core 0 to core 1, status back
//   wifi_setup.h own network / home network, saved settings
//   web.h        web server routes;  page.h  the page itself
//   log_meta.h   the "# setting" lines at the top of every log file
//   secrets.h    own-network password (not in git; copy secrets.example.h)

#include "config.h"
#include "imu.h"
#include "angle.h"
#include "motors.h"
#include "control.h"
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

// ---- the motor test (step 5) ----
// Core 0 asks for one (shared.h); core 1 starts it between ticks, sets the PWM
// every tick while it runs, and stops it when the time is up or Stop is asked.
struct MotorTest {
  bool running = false;
  int  left = 0, right = 0;                 // PWM, forward +
  unsigned long startMs = 0, durationMs = 0;
  int32_t startL = 0, startR = 0;           // encoder counts when it started
};
MotorTest test;

void motorTestStart() {
  test.left  = constrain((int)testLeftPwm,  -MOTOR_TEST_MAX_PWM, MOTOR_TEST_MAX_PWM);
  test.right = constrain((int)testRightPwm, -MOTOR_TEST_MAX_PWM, MOTOR_TEST_MAX_PWM);
  test.durationMs = min((unsigned long)testMs, (unsigned long)MOTOR_TEST_MAX_MS);
  test.startMs = millis();
  test.startL = encoderLeft();
  test.startR = encoderRight();
  test.running = true;
  motorTestRunning = true;
  motorsEnable(true);                       // wake the driver; motorsDrive() runs every tick
}

void motorTestStop() {
  motorsOff();                              // brake, then STBY low
  test.running = false;
  lastTestMovedL = encoderLeft() - test.startL;
  lastTestMovedR = encoderRight() - test.startR;
  lastTestRanMs = millis() - test.startMs;
  motorTestRunning = false;
  motorTestsDone = motorTestsDone + 1;      // core 0 sees this and reports the result
}

void setup1() {
  motorsBegin();           // first: driver asleep, encoders counting
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
  if (requestMotorStop) {
    requestMotorStop = false;
    requestMotorTest = false;
    if (test.running) motorTestStop();
  }
  if (requestDisarm) {
    requestDisarm = false;
    requestArm = false;
    balanceDisarm(requestDisarmReason);
    requestDisarmReason = REASON_USER;
    balance.fallen = false;
  }
  if (requestArm) {
    requestArm = false;
    balanceArm(angle.pitch, test.running);
    armAttempts = armAttempts + 1;             // core 0 reports armed / refused when this changes
  }
  if (requestMotorTest) {
    requestMotorTest = false;
    if (!test.running && !balance.armed) motorTestStart();   // never a motor test while balancing
  }
  if (requestCalibrate) {
    if (test.running) motorTestStop();         // never calibrate with the wheels spinning
    if (balance.armed) balanceDisarm(REASON_CALIBRATE);
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
  r.flags = overranLastTick ? FLAG_OVERRUN : 0;
  if (!everCalibrated) r.flags |= FLAG_NO_BIAS;

  bool imuOk = imuRead(s);
  if (imuOk) {
    angleUpdate(angle, s, dt);
  } else {
    r.flags |= FLAG_IMU_ERROR;
  }

  for (int i = 0; i < 7; i++) r.raw[i] = s.raw[i];
  r.pitch       = angle.pitch;
  r.pitch_rate  = angle.pitchRate;
  r.accel_pitch = angle.accelPitch;

  // Wheels: encoders and speed every tick, motors only during a test.
  static WheelSpeed speedL, speedR;
  r.enc_l   = encoderLeft();
  r.enc_r   = encoderRight();
  r.speed_l = (int16_t)constrain(speedL.update(r.enc_l), -32000.0f, 32000.0f);
  r.speed_r = (int16_t)constrain(speedR.update(r.enc_r), -32000.0f, 32000.0f);
  if (test.running && millis() - test.startMs >= test.durationMs) motorTestStop();   // time's up

  // Balance loop (does nothing unless armed). Same PWM to both wheels: no steering yet.
  int pwm = balanceTick(angle, imuOk, overranLastTick, dt, r);

  if (test.running) {
    motorsDrive(test.left, test.right);
    r.pwm_l = test.left;
    r.pwm_r = test.right;
  } else if (balance.armed) {
    motorsDrive(pwm, pwm);
    r.pwm_l = r.pwm_r = pwm;
  } else {
    r.pwm_l = r.pwm_r = 0;
  }
  r.mode = test.running ? MODE_MOTOR_TEST : balanceMode();
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
  Serial.println("BB1 step 6 commands:");
  Serial.println("  cal       measure the gyro bias again (lay the robot still first)");
  Serial.println("  alpha N   set the filter weight, 0.90 to 0.999 (now " + String(requestedAlpha, 3) + ")");
  Serial.println("  csv       switch the readout to comma-separated lines (again to switch back)");
  Serial.println("  dump      print the whole log buffer (about the last " + String(LOG_SECONDS - 1) + " s) as CSV");
  Serial.println("  dump N    print only the last N seconds");
  Serial.println("  wifi      show the network and the page address");
  Serial.println("  wifi reset  forget the home network and restart on the own network");
  Serial.println("  quiet     stop / restart the 10 Hz readout (easier to read other output)");
  Serial.println("  m L R [ms]  motor test: left and right PWM (-" + String(MOTOR_TEST_MAX_PWM) + ".." + String(MOTOR_TEST_MAX_PWM) +
                 ", forward +) for ms (default 2000, max " + String(MOTOR_TEST_MAX_MS) + "). WHEELS OFF THE TABLE.");
  Serial.println("            e.g. m 100 100 = both forward 2 s;  m 0 -80 1000 = right backward 1 s");
  Serial.println("  stop      stop the motors now (so does pressing Enter during a test)");
  Serial.println("  enc       print both encoder counts (turn a wheel one turn by hand to check 520)");
  Serial.println("  arm [N]   start balancing (hold it near upright first); N = stop after N s. disarm, or Enter, stops it");
  Serial.println("  kp N / ki N / kd N   set a gain (now " + String(gainKp, 2) + " / " + String(gainKi, 2) + " / " + String(gainKd, 2) + ")");
  Serial.println("  trim N    balance point in deg (now " + String(balanceTrim, 2) + ");  trim here = use the current pitch");
  Serial.println("  cap N     PWM limit while armed (now " + String(pwmCap, 0) + ");  deadband N  (now " + String(motorDeadband, 0) + ")");
  Serial.println("  gains     print the balance settings");
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

void printGains() {
  Serial.println("gains v" + String(gainsVersion) + ": kp " + String(gainKp, 2) + "  ki " + String(gainKi, 2) +
                 "  kd " + String(gainKd, 2) + "  trim " + String(balanceTrim, 2) + " deg  cap " + String(pwmCap, 0) +
                 "  deadband " + String(motorDeadband, 0));
}

// The newest pitch, read from the log like everything else on core 0.
float latestPitch() {
  LogRecord r;
  return (totalWritten && logGet(totalWritten - 1, r)) ? r.pitch : 0;
}

// "kp 15", "trim here", ... Each change bumps gainsVersion so logs show it.
bool handleGainCommand(const String &cmd) {
  float v = cmd.substring(cmd.indexOf(' ') + 1).toFloat();
  if      (cmd.startsWith("kp "))       gainKp = constrain(v, 0.0f, 100.0f);
  else if (cmd.startsWith("ki "))       gainKi = constrain(v, 0.0f, 100.0f);
  else if (cmd.startsWith("kd "))       gainKd = constrain(v, 0.0f, 20.0f);
  else if (cmd == "trim here")          balanceTrim = latestPitch();
  else if (cmd.startsWith("trim "))     balanceTrim = constrain(v, -20.0f, 20.0f);
  else if (cmd.startsWith("cap "))      pwmCap = constrain(v, 0.0f, (float)PWM_MAX);
  else if (cmd.startsWith("deadband ")) motorDeadband = constrain(v, 0.0f, 100.0f);
  else return false;
  gainsVersion = gainsVersion + 1;
  printGains();
  return true;
}

// "m L R [ms]": read up to three numbers and ask core 1 to run the test.
void startMotorTest(String args) {
  int l = 0, r = 0, ms = 2000;
  int n = sscanf(args.c_str(), "%d %d %d", &l, &r, &ms);
  if (n < 2) {
    Serial.println("usage: m LEFT RIGHT [ms], e.g. m 100 100 2000");
    return;
  }
  testLeftPwm  = constrain(l, -MOTOR_TEST_MAX_PWM, MOTOR_TEST_MAX_PWM);
  testRightPwm = constrain(r, -MOTOR_TEST_MAX_PWM, MOTOR_TEST_MAX_PWM);
  testMs       = constrain(ms, 0, MOTOR_TEST_MAX_MS);
  requestMotorTest = true;                     // last: core 1 reads the numbers after seeing this
  Serial.println("Motor test: left " + String(testLeftPwm) + ", right " + String(testRightPwm) +
                 " for " + String(testMs) + " ms. Press Enter to stop early.");
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
  } else if (cmd == "stop") {
    requestMotorStop = true;
    requestDisarm = true;
  } else if (cmd == "arm" || cmd.startsWith("arm ")) {
    armedFromWeb = false;                      // armed from serial: Enter is your stop button
    float seconds = cmd.length() > 4 ? cmd.substring(4).toFloat() : 0;
    armTimeMs = (uint32_t)(constrain(seconds, 0.0f, 600.0f) * 1000);   // "arm 5" = disarm by itself after 5 s
    requestArm = true;
  } else if (cmd == "disarm") {
    requestDisarm = true;
  } else if (cmd == "gains") {
    printGains();
  } else if (handleGainCommand(cmd)) {
    // done
  } else if (cmd.startsWith("m ")) {
    startMotorTest(cmd.substring(2));
  } else if (cmd == "enc") {
    LogRecord r;
    if (totalWritten && logGet(totalWritten - 1, r))
      Serial.println("encoders: left " + String(r.enc_l) + ", right " + String(r.enc_r) + " counts (forward +, " +
                     String(COUNTS_PER_REV) + " per turn assumed)");
  } else if (cmd.startsWith("dump")) {
    dumpLog(cmd.substring(4).toFloat());       // "dump" alone gives 0 = everything
  } else {
    printHelp();
  }
}

String line;
void readSerial() {
  static char prev = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' && prev == '\r') { prev = c; continue; }   // "\r\n" is one Enter, not two
    prev = c;
    if (c == '\n' || c == '\r') {
      if (line.length() == 0 && motorTestRunning) requestMotorStop = true;   // Enter = stop
      if (line.length() == 0 && robotArmed) requestDisarm = true;           // Enter = disarm
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
  Serial.println("BB1 step 6: arm / disarm + angle loop. Motors run only when armed or in a timed test.");
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

  // Report each finished motor test (core 1 counts them up).
  static uint32_t testsSeen = 0;
  if (motorTestsDone != testsSeen) {
    testsSeen = motorTestsDone;
    long ms = max((long)lastTestRanMs, 1L);
    Serial.println("Motor test done after " + String(ms) + " ms: left moved " + String(lastTestMovedL) +
                   " counts (" + String(lastTestMovedL * 1000L / ms) + "/s), right " + String(lastTestMovedR) +
                   " counts (" + String(lastTestMovedR * 1000L / ms) + "/s)");
  }

  // Report arming and disarming (core 1 sets robotArmed, disarmReason, armAttempts).
  static bool armedSeen = false;
  static uint32_t attemptsSeen = 0;
  if (armAttempts != attemptsSeen) {           // an arm request was handled
    attemptsSeen = armAttempts;
    Serial.println(robotArmed ? "ARMED: balancing. Enter or 'disarm' to stop."
                              : String("not armed: ") + reasonText(disarmReason));
  } else if (armedSeen && !robotArmed) {       // it was balancing and stopped
    Serial.println(String("DISARMED: ") + reasonText(disarmReason));
  }
  armedSeen = robotArmed;

  readSerial();
  wifiLoop();
  webLoop();

  static unsigned long lastPrint = 0;
  if (!quiet && millis() - lastPrint >= 1000 / PRINT_HZ) {
    lastPrint = millis();
    printStatus();
  }
}
