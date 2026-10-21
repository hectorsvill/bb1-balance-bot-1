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
//
// Each handler is short and never touches the IMU or motors directly: it reads
// the log buffer or sets a request in shared.h, exactly like the serial commands.

#pragma once
#include <WebServer.h>
#include "telemetry.h"
#include "shared.h"
#include "wifi_setup.h"
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
           "\"alpha\":%.3f,\"bias\":[%.2f,%.2f,%.2f],\"calibrations\":%lu,\"cal_ok\":%s,\"imu\":%s,"
           "\"wifi_mode\":\"%s\",\"wifi_ssid\":\"%s\",\"ip\":\"%s\",\"uptime_s\":%lu}",
           have ? "true" : "false",
           have ? (unsigned long)r.tick : 0UL, have ? (unsigned long)r.t_us : 0UL,
           have ? r.mode : 0, have ? r.flags : 0,
           have ? r.pitch : 0.0, have ? r.accel_pitch : 0.0, have ? r.pitch_rate : 0.0,
           have ? r.work_us : 0,
           requestedAlpha, gyroBias[0], gyroBias[1], gyroBias[2],
           (unsigned long)calibrationsDone, lastCalibrationOk ? "true" : "false", imuFound ? "true" : "false",
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

void webBegin() {
  server.on("/", HTTP_GET, handlePage);
  server.on("/state", HTTP_GET, handleState);
  server.on("/cal", HTTP_POST, handleCal);
  server.on("/alpha", HTTP_POST, handleAlpha);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/wifi/forget", HTTP_POST, handleWifiForget);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

// Call from loop() on core 0.
void webLoop() {
  server.handleClient();
  if (restartAt && (long)(millis() - restartAt) >= 0) rp2040.reboot();
}
