// web.h - the web server: the page, live data, and actions.
//
// Runs on core 0. The browser asks for things with plain HTTP:
//
//   GET  /            the page (page.h)
//   GET  /tune        the small PID tuning page (tune_page.h): gains + live P/I/D, never arms
//   GET  /state       newest values as JSON, polled ~20 times a second by the page
//   POST /cal         recalibrate the gyro (same as typing "cal")
//   POST /alpha       body/arg value=0.98: set the filter weight
//   GET  /wifi        current network settings (never the password)
//   POST /wifi        args ssid, password: save a home network and restart
//   POST /wifi/forget forget the home network and restart on the own network
//   GET  /log/meta    the "# setting" lines for the top of a log file (log_meta.h)
//   GET  /log?since=N log records from tick N on, as CSV (see handleLog)
//   GET  /sys         RAM and flash use as JSON (the page's System panel)
//   POST /motor       args left, right, ms: timed motor test (same as serial "m L R ms")
//   POST /motor/stop  stop the motors now (and disarm)
//   POST /arm         start balancing (refused unless upright; see control.h). seconds=N: disarm after N s (0 = no limit);
//                     heartbeat=0: don't stop when the page goes away (main page)
//   POST /disarm      stop balancing
//   POST /gains       any of kp, ki, kd, trim (a number or "here"), cap, deadband, sp, si, th, auto, dff
//   POST /gains/save  store them in flash (disarmed only);  POST /gains/defaults  back to config.h
//   POST /step        step test while armed: setpoint +2 deg for 1 s
//   POST /drive       args speed, turn (wheel turns/s), ms: drive while balancing (drive.h). Lasts 500 ms unless re-sent
//   POST /drive/stop  stop driving now (keeps balancing)
//
// docs/API.md is the full reference (arguments, answers, units, safety rules).
// Each handler is short and never touches the IMU or motors directly: it reads
// the log buffer or sets a request in shared.h, exactly like the serial commands.

#pragma once
#include <WebServer.h>
#include "telemetry.h"
#include "shared.h"
#include "wifi_setup.h"
#include "log_meta.h"
#include "control.h"
#include "auto_arm.h"
#include "settings.h"
#include "page.h"
#include "tune_page.h"
#include "battery.h"
#include "drive.h"

WebServer server(80);

// Restart after a reply has had time to reach the browser.
unsigned long restartAt = 0;

void handlePage() {
  server.send(200, "text/html", PAGE_HTML);
}

// The small PID tuning page (tune_page.h): gains, live P/I/D, notes. Never arms.
void handleTunePage() {
  server.send(200, "text/html", TUNE_HTML);
}

void handleState() {
  lastWebHeartbeatMs = millis();               // the page is still there (see HEARTBEAT_MS)
  // Time left on the arm timer, for the page's countdown (-1 = no timer running).
  long armLeftMs = (robotArmed && armTimeMs) ? (long)armTimeMs - (long)(millis() - armedSinceMs) : -1;
  if (armLeftMs < -1) armLeftMs = 0;
  // Time left on the drive command (0 = not driving, or it ran out).
  uint32_t until = driveUntilMs;
  long driveLeftMs = until ? max(0L, (long)(int32_t)(until - millis())) : 0;
  LogRecord r;
  uint32_t total = totalWritten;
  bool have = total > 0 && logGet(total - 1, r);

  char json[1400];
  snprintf(json, sizeof(json),
           "{\"have\":%s,\"tick\":%lu,\"t_us\":%lu,\"mode\":%u,\"flag_bits\":%u,"
           "\"pitch\":%.2f,\"accel_pitch\":%.2f,\"pitch_rate\":%.2f,\"work_us\":%u,"
           "\"alpha\":%.3f,\"bias\":[%.2f,%.2f,%.2f],\"calibrations\":%lu,\"cal_ok\":%s,\"ever_cal\":%s,\"imu\":%s,"
           "\"wifi_mode\":\"%s\",\"wifi_ssid\":\"%s\",\"ip\":\"%s\",\"uptime_s\":%lu,"
           "\"enc_l\":%ld,\"enc_r\":%ld,\"speed_l\":%d,\"speed_r\":%d,\"pwm_l\":%d,\"pwm_r\":%d,"
           "\"test_running\":%s,\"tests_done\":%lu,\"test_moved\":[%ld,%ld],\"test_ms\":%lu,"
           "\"armed\":%s,\"reason\":\"%s\",\"arm_attempts\":%lu,\"arm_ms\":%lu,\"arm_left_ms\":%ld,\"step\":%s,\"auto_state\":\"%s\",\"p\":%.1f,\"i\":%.1f,\"d\":%.1f,"
           "\"enc_fault\":%u,"
           "\"drive\":{\"speed\":%.2f,\"turn\":%.2f,\"speed_now\":%.2f,\"turn_now\":%.2f,\"left_ms\":%ld,\"commands\":%lu,\"max_speed\":%.2f,\"max_turn\":%.2f},"
           "\"batt_v\":%.2f,\"batt_ok\":%s,\"batt_warn_v\":%.2f,\"batt_low_v\":%.2f,\"batt_cells\":%d,"
           "\"gains\":{\"ver\":%lu,\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f,\"trim\":%.2f,\"cap\":%.0f,\"deadband\":%.0f,\"sp\":%.2f,\"si\":%.2f,\"th\":%.1f,\"auto\":%d,\"dff\":%.0f}}",
           have ? "true" : "false",
           have ? (unsigned long)r.tick : 0UL, have ? (unsigned long)r.t_us : 0UL,
           have ? r.mode : 0, have ? r.flags : 0,
           have ? r.pitch : 0.0, have ? r.accel_pitch : 0.0, have ? r.pitch_rate : 0.0,
           have ? r.work_us : 0,
           requestedAlpha, gyroBias[0], gyroBias[1], gyroBias[2],
           (unsigned long)calibrationsDone, lastCalibrationOk ? "true" : "false", everCalibrated ? "true" : "false", imuFound ? "true" : "false",
           wifiMode == WIFI_HOME_NETWORK ? "home" : "own",
           wifiMode == WIFI_HOME_NETWORK ? wifiSettings.ssid : BB1_AP_SSID,
           wifiAddress().c_str(), millis() / 1000,
           have ? (long)r.enc_l : 0L, have ? (long)r.enc_r : 0L, have ? r.speed_l : 0, have ? r.speed_r : 0,
           have ? r.pwm_l : 0, have ? r.pwm_r : 0,
           motorTestRunning ? "true" : "false", (unsigned long)motorTestsDone,
           (long)lastTestMovedL, (long)lastTestMovedR, (unsigned long)lastTestRanMs,
           robotArmed ? "true" : "false", reasonText(disarmReason), (unsigned long)armAttempts, (unsigned long)armTimeMs, armLeftMs, stepActive ? "true" : "false", autoStateText(autoArmState),
           have ? r.p_x10 / 10.0 : 0.0, have ? r.i_x10 / 10.0 : 0.0, have ? r.d_x10 / 10.0 : 0.0,
           encoderFault,
           driveLeftMs > 0 ? driveSpeedCmd : 0.0, driveLeftMs > 0 ? driveTurnCmd : 0.0, driveSpeedNow, driveTurnNow, driveLeftMs,
           (unsigned long)driveCommands, DRIVE_MAX_SPEED, DRIVE_MAX_TURN,
           batteryMv / 1000.0, batteryPresent() ? "true" : "false", BATTERY_WARN_V, BATTERY_LOW_V, BATTERY_CELLS,
           (unsigned long)gainsVersion, gainP, gainI, gainD, balanceTrim, pwmCap, motorDeadband, gainSpeedP, gainSpeedI, gainTurnHold, (int)autoArm, gainDriveFF);
  server.send(200, "application/json", json);
}

void handleCal() {
  requestCalibrate = true;
  server.send(200, "text/plain", "calibrating, keep the robot still for 2 s");
}

void handleAlpha() {
  float a = server.arg("value").toFloat();
  if (a < 0.90 || a > 0.999) {
    server.send(400, "text/plain", "alpha must be between 0.90 and 0.999");
    return;
  }
  requestedAlpha = a;
  server.send(200, "text/plain", "alpha = " + String(a, 3));
}

void handleWifiGet() {
  String json = "{\"mode\":\"" + String(wifiMode == WIFI_HOME_NETWORK ? "home" : "own") +
                "\",\"saved_ssid\":\"" + String(homeNetworkSaved() ? wifiSettings.ssid : "") +
                "\",\"note\":\"" + wifiNote + "\"}";
  server.send(200, "application/json", json);
}

void handleWifiSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63 ||
      (password.length() > 0 && password.length() < 8)) {
    server.send(400, "text/plain", "need a network name (1-32 chars) and a password of 8-63 chars (or none for an open network)");
    return;
  }
  wifiSaveHomeNetwork(ssid, password);
  server.send(200, "text/plain", "saved. Restarting to join \"" + ssid + "\". Then open http://bb1.local");
  Serial.println("Home network \"" + ssid + "\" saved from the web page, restarting...");
  restartAt = millis() + 1000;
}

void handleWifiForget() {
  wifiForgetHomeNetwork();
  server.send(200, "text/plain", "forgotten. Restarting on the own network \"" + String(BB1_AP_SSID) + "\"");
  Serial.println("Home network forgotten from the web page, restarting...");
  restartAt = millis() + 1000;
}

void handleLogMeta() {
  server.send(200, "text/plain", logMetaLines());
}

// GET /log?since=N  ->  every record from tick N on (up to LOG_MAX_PER_REQUEST).
//
// Whoever is recording (the page, tools/logger.py) remembers "to" from the
// first line and asks for since=<to> next time, so it gets each record once:
//
//   # from,1200,to,1450,lost,0       <- records 1200..1449 follow; ask since=1450 next
//   tick,t_us,...                    <- CSV header
//   1200,6012345,451,...             <- one line per record
//
// "lost" counts records that were already overwritten in the ring buffer
// because the asker waited too long (more than LOG_SECONDS). since=0 means
// "from the oldest record I still have" and never counts as lost.
const uint32_t LOG_MAX_PER_REQUEST = 1000;    // 5 s of records, about 90 KB of text

void handleLog() {
  uint32_t total = totalWritten;
  uint32_t oldest = logOldest();
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  bool restarted = since > total;               // the robot restarted since the last request
  if (restarted) since = oldest;

  uint32_t first = max(since, oldest);
  uint32_t lost = (since > 0 && since < oldest) ? oldest - since : 0;
  uint32_t last = min(total, first + LOG_MAX_PER_REQUEST);   // one past the last record sent

  // Send it in pieces ("chunked") instead of building one huge string in RAM.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  char chunk[2048];
  int len = snprintf(chunk, sizeof(chunk), "# from,%lu,to,%lu,lost,%lu%s\n%s\n",
                     (unsigned long)first, (unsigned long)last, (unsigned long)lost,
                     restarted ? ",restarted,1" : "", CSV_HEADER);
  LogRecord r;
  for (uint32_t n = first; n < last; n++) {
    if (!logGet(n, r)) continue;                // overwritten while we were sending: skip
    if (len > (int)sizeof(chunk) - 240) {       // nearly full: send what we have (a line is < 240)
      server.sendContent(chunk, len);
      len = 0;
    }
    len += formatCsv(chunk + len, sizeof(chunk) - len, r);
  }
  if (len) server.sendContent(chunk, len);
  server.sendContent("");                       // empty piece = end of the response
}

// POST /motor left=100&right=100&ms=2000 -> a timed motor test.
// Out-of-range numbers are clamped to the limits in config.h, never refused
// silently: the answer says what will actually run.
void handleMotor() {
  if (robotArmed) {
    server.send(409, "text/plain", "disarm first: no motor test while balancing");
    return;
  }
  if (motorTestRunning) {
    server.send(409, "text/plain", "a motor test is already running");
    return;
  }
  int l = server.arg("left").toInt(), r = server.arg("right").toInt();
  int ms = server.hasArg("ms") ? server.arg("ms").toInt() : 2000;
  testLeftPwm  = constrain(l, -MOTOR_TEST_MAX_PWM, MOTOR_TEST_MAX_PWM);
  testRightPwm = constrain(r, -MOTOR_TEST_MAX_PWM, MOTOR_TEST_MAX_PWM);
  testMs       = constrain(ms, 0, MOTOR_TEST_MAX_MS);
  requestMotorTest = true;                     // last: core 1 reads the numbers after seeing this
  server.send(200, "text/plain", "running: left " + String(testLeftPwm) + ", right " + String(testRightPwm) +
                                 " for " + String(testMs) + " ms");
}

void handleMotorStop() {
  requestMotorStop = true;
  requestDisarm = true;                        // the page's STOP / space bar stops everything
  server.send(200, "text/plain", "stopped");
}

// POST /arm: core 1 decides whether it's safe (control.h) and the page shows
// the answer from /state ("armed" and "reason") a moment later.
void handleArm() {
  // heartbeat=0 (the main page): keep balancing even if the page closes, like
  // auto-arm. Otherwise (/tune test runs) the page must keep polling /state.
  armedFromWeb = server.arg("heartbeat") != "0";
  lastWebHeartbeatMs = millis();
  float seconds = server.hasArg("seconds") ? server.arg("seconds").toFloat() : 0;
  armTimeMs = (uint32_t)(constrain(seconds, 0.0f, ARM_SECONDS_MAX) * 1000);
  requestArm = true;
  server.send(200, "text/plain", "arming...");
}

void handleDisarm() {
  if (server.arg("why") == "hidden") requestDisarmReason = REASON_PAGE_HIDDEN;
  requestDisarm = true;
  server.send(200, "text/plain", "disarmed");
}

// POST /gains kp=15&kd=0.5 ... Only the fields sent are changed, with the same
// names and limits as serial (setBalanceSetting in settings.h). "trim=here"
// uses the current pitch.
void handleGains() {
  for (int n = 0; n < server.args(); n++) {
    if (server.arg(n).length()) setBalanceSetting(server.argName(n), server.arg(n));   // unknown names are ignored
  }
  gainsVersion = gainsVersion + 1;
  printGains();                                // tell the terminal too (handheld.py logs it)
  server.send(200, "text/plain", gainsLine());
}

void handleGainsSave() {
  String msg = settingsSave();
  server.send(msg.startsWith("saved") ? 200 : 409, "text/plain", msg);
}

void handleGainsDefaults() {
  settingsDefaults();
  printGains();
  server.send(200, "text/plain", "back to the config.h defaults (press Save to keep them)");
}

void handleStep() {
  if (!robotArmed) {
    server.send(409, "text/plain", "arm first: the step test moves the setpoint while balancing");
    return;
  }
  requestStep = true;
  server.send(200, "text/plain", "step: setpoint +" + String(STEP_TEST_DEG, 1) + " deg for " + String(STEP_TEST_MS) + " ms");
}

// POST /drive speed=0.5&turn=0&ms=500 -> drive while balancing (drive.h).
//   speed  wheel turns/s, forward +, clamped to +-DRIVE_MAX_SPEED
//   turn   turns/s of left-minus-right wheel difference, right +, clamped to +-DRIVE_MAX_TURN
//   ms     how long this command lasts (default DRIVE_HOLD_MS, max DRIVE_HOLD_MAX_MS).
//          Send again before it runs out to keep going; when it runs out BB1
//          ramps to a stop and keeps balancing. That's the Wi-Fi failsafe.
// Missing speed or turn = 0. Answers JSON with what will actually run, so a
// program (or an AI agent) can see any clamping. 409 when not balancing.
void handleDrive() {
  if (!robotArmed) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"not balancing: arm first (POST /arm)\"}");
    return;
  }
  if (encoderFault) {
    server.send(409, "application/json", "{\"ok\":false,\"error\":\"encoder fault: a wheel is not counting, check its wires (see /state enc_fault)\"}");
    return;
  }
  float speed = constrain(server.arg("speed").toFloat(), -DRIVE_MAX_SPEED, DRIVE_MAX_SPEED);
  float turn  = constrain(server.arg("turn").toFloat(),  -DRIVE_MAX_TURN,  DRIVE_MAX_TURN);
  long ms = server.hasArg("ms") ? server.arg("ms").toInt() : (long)DRIVE_HOLD_MS;
  ms = constrain(ms, 0L, (long)DRIVE_HOLD_MAX_MS);
  driveSpeedCmd = speed;                       // the numbers first...
  driveTurnCmd  = turn;
  driveUntilMs  = ms ? millis() + ms : 0;      // ...then the deadline: core 1 reads them after seeing it
  driveCommands = driveCommands + 1;
  char json[120];
  snprintf(json, sizeof(json), "{\"ok\":true,\"speed\":%.2f,\"turn\":%.2f,\"ms\":%ld}", speed, turn, ms);
  server.send(200, "application/json", json);
}

// POST /drive/stop -> ramp to a stop now and keep balancing. Always allowed.
void handleDriveStop() {
  driveUntilMs = 0;
  server.send(200, "application/json", "{\"ok\":true,\"speed\":0,\"turn\":0,\"ms\":0}");
}

// GET /sys -> how much of the Pico's memory the firmware uses.
//
// RAM (264 KB on the RP2040, 256 KB of it for the program) is split in two:
//   static: global variables, fixed at compile time. The 80 KB log buffer is
//           the biggest one; Wi-Fi's buffers are next.
//   heap:   the rest, handed out while running (Strings, Wi-Fi packets, web
//           requests). "heap free" is what's left for those.
// Flash (2 MB) holds the program, including this page, and the saved settings.
// __flash_binary_end is a marker the linker puts right after the program.
extern uint8_t __flash_binary_end;
const uint32_t FLASH_TOTAL = 2 * 1024 * 1024;   // the Pico W's flash chip
const uint32_t RAM_TOTAL   = 256 * 1024;        // main RAM (the other 8 KB hold the two stacks)

void handleSys() {
  uint32_t heapTotal = rp2040.getTotalHeap();
  uint32_t heapUsed  = rp2040.getUsedHeap();
  uint32_t flashUsed = (uint32_t)&__flash_binary_end - XIP_BASE;   // XIP_BASE = where flash starts in memory
  char json[256];
  snprintf(json, sizeof(json),
           "{\"ram_total\":%lu,\"ram_static\":%lu,\"heap_total\":%lu,\"heap_used\":%lu,"
           "\"log_buffer\":%lu,\"flash_total\":%lu,\"flash_used\":%lu,\"page\":%lu,\"cpu_mhz\":%lu}",
           (unsigned long)RAM_TOTAL, (unsigned long)(RAM_TOTAL - heapTotal),
           (unsigned long)heapTotal, (unsigned long)heapUsed,
           (unsigned long)sizeof(logBuffer), (unsigned long)FLASH_TOTAL, (unsigned long)flashUsed,
           (unsigned long)sizeof(PAGE_HTML), (unsigned long)(rp2040.f_cpu() / 1000000));
  server.send(200, "application/json", json);
}

// Why every route goes through route() instead of server.on():
// the WebServer library serves one browser connection at a time. After it
// answers, it waits up to 5 s (HTTP_MAX_CLOSE_WAIT) for the browser to close
// that connection, and browsers don't always close it right away (for example
// after the page gave up on a slow /state). Meanwhile every other request
// waits in line, /state stops arriving, and an armed robot disarms with
// "lost the web page". Closing the connection ourselves after each answer
// frees the server for the next request at once.
void route(const char *path, HTTPMethod method, void (*handler)()) {
  server.on(path, method, [handler]() {
    handler();
    server.client().stop();                    // sends anything left, then closes
  });
}

void webBegin() {
  route("/", HTTP_GET, handlePage);
  route("/tune", HTTP_GET, handleTunePage);
  route("/state", HTTP_GET, handleState);
  route("/cal", HTTP_POST, handleCal);
  route("/alpha", HTTP_POST, handleAlpha);
  route("/wifi", HTTP_GET, handleWifiGet);
  route("/wifi", HTTP_POST, handleWifiSave);
  route("/wifi/forget", HTTP_POST, handleWifiForget);
  route("/log/meta", HTTP_GET, handleLogMeta);
  route("/log", HTTP_GET, handleLog);
  route("/sys", HTTP_GET, handleSys);
  route("/motor", HTTP_POST, handleMotor);
  route("/motor/stop", HTTP_POST, handleMotorStop);
  route("/arm", HTTP_POST, handleArm);
  route("/disarm", HTTP_POST, handleDisarm);
  route("/gains", HTTP_POST, handleGains);
  route("/gains/save", HTTP_POST, handleGainsSave);
  route("/gains/defaults", HTTP_POST, handleGainsDefaults);
  route("/step", HTTP_POST, handleStep);
  route("/drive", HTTP_POST, handleDrive);
  route("/drive/stop", HTTP_POST, handleDriveStop);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); server.client().stop(); });
  server.begin();
}

// Call from loop() on core 0.
void webLoop() {
  server.handleClient();
  if (restartAt && (long)(millis() - restartAt) >= 0) rp2040.reboot();
}
