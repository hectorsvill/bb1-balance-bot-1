// web.h - the web server: the page, live data, and actions.
//
// Runs on core 0. The browser asks for things with plain HTTP:
//
//   GET  /            the page (page.h)
//   GET  /state       newest values as JSON, polled ~20 times a second by the page
//   POST /cal         recalibrate the gyro (same as typing "cal")
//   POST /alpha       body/arg value=0.98: set the filter weight
//   GET  /wifi        current network settings (never the password)
//   POST /wifi        args ssid, password: save a home network and restart
//   POST /wifi/forget forget the home network and restart on the own network
//   GET  /log/meta    the "# setting" lines for the top of a log file (log_meta.h)
//   GET  /log?since=N log records from tick N on, as CSV (see handleLog)
//   GET  /sys         RAM and flash use as JSON (the page's System panel)
//
// Each handler is short and never touches the IMU or motors directly: it reads
// the log buffer or sets a request in shared.h, exactly like the serial commands.

#pragma once
#include <WebServer.h>
#include "telemetry.h"
#include "shared.h"
#include "wifi_setup.h"
#include "log_meta.h"
#include "page.h"

WebServer server(80);

// Restart after a reply has had time to reach the browser.
unsigned long restartAt = 0;

void handlePage() {
  server.send(200, "text/html", PAGE_HTML);
}

void handleState() {
  LogRecord r;
  uint32_t total = totalWritten;
  bool have = total > 0 && logGet(total - 1, r);

  char json[512];
  snprintf(json, sizeof(json),
           "{\"have\":%s,\"tick\":%lu,\"t_us\":%lu,\"mode\":%u,\"flag_bits\":%u,"
           "\"pitch\":%.2f,\"accel_pitch\":%.2f,\"pitch_rate\":%.2f,\"work_us\":%u,"
           "\"alpha\":%.3f,\"bias\":[%.2f,%.2f,%.2f],\"calibrations\":%lu,\"cal_ok\":%s,\"ever_cal\":%s,\"imu\":%s,"
           "\"wifi_mode\":\"%s\",\"wifi_ssid\":\"%s\",\"ip\":\"%s\",\"uptime_s\":%lu}",
           have ? "true" : "false",
           have ? (unsigned long)r.tick : 0UL, have ? (unsigned long)r.t_us : 0UL,
           have ? r.mode : 0, have ? r.flags : 0,
           have ? r.pitch : 0.0, have ? r.accel_pitch : 0.0, have ? r.pitch_rate : 0.0,
           have ? r.work_us : 0,
           requestedAlpha, gyroBias[0], gyroBias[1], gyroBias[2],
           (unsigned long)calibrationsDone, lastCalibrationOk ? "true" : "false", everCalibrated ? "true" : "false", imuFound ? "true" : "false",
           wifiMode == WIFI_HOME_NETWORK ? "home" : "own",
           wifiMode == WIFI_HOME_NETWORK ? wifiSettings.ssid : BB1_AP_SSID,
           wifiAddress().c_str(), millis() / 1000);
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
    if (len > (int)sizeof(chunk) - 160) {       // nearly full: send what we have
      server.sendContent(chunk, len);
      len = 0;
    }
    len += formatCsv(chunk + len, sizeof(chunk) - len, r);
  }
  if (len) server.sendContent(chunk, len);
  server.sendContent("");                       // empty piece = end of the response
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

void webBegin() {
  server.on("/", HTTP_GET, handlePage);
  server.on("/state", HTTP_GET, handleState);
  server.on("/cal", HTTP_POST, handleCal);
  server.on("/alpha", HTTP_POST, handleAlpha);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/wifi/forget", HTTP_POST, handleWifiForget);
  server.on("/log/meta", HTTP_GET, handleLogMeta);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/sys", HTTP_GET, handleSys);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

// Call from loop() on core 0.
void webLoop() {
  server.handleClient();
  if (restartAt && (long)(millis() - restartAt) >= 0) rp2040.reboot();
}
