// ota.h - firmware updates over Wi-Fi (OTA = over the air), no USB cable.
//
//   POST /update   multipart form, field "firmware" = build/bb1/bb1.ino.bin
//                  (or the same file gzipped), Basic auth user "bb1" and
//                  BB1_OTA_PASSWORD from secrets.h. tools/flash.py --ota does it all.
//
// How it works (arduino-pico's Updater + the small OTA bootloader the core
// always puts at the start of flash):
//   1. the new firmware arrives in pieces and is written to a file in the
//      LittleFS area of flash (the top 1 MB: that's why the board's Flash Size
//      is "Sketch: 1MB, FS: 1MB"; see FLASH_FS_BYTES below)
//   2. when the last piece is in, the Updater checks the size and leaves a
//      note for the bootloader ("copy firmware.bin"), then BB1 restarts
//   3. the bootloader copies the file over the old program and starts it.
//      If the power goes during the copy, the note is still there and it
//      copies again on the next power-on. Only a firmware that can't reach
//      Wi-Fi needs USB again (and BOOTSEL always works: docs/FLASHING.md).
// An upload that breaks off halfway changes nothing: the old firmware keeps
// running and the note is never written.
//
// Safety: same rule as flash.py over USB. Refused unless BB1 is disarmed, no
// motor test runs, and it's lying down (more than FALL_ANGLE from the trim):
// after the restart, auto-arm would start balancing an upright robot on the
// spot. While the upload runs, otaActive (shared.h) keeps core 1 from arming,
// because writing flash pauses core 1 for a few ms at a time: the balance
// loop must not be running then. (Saving the gains pauses it the same way,
// which is why /gains/save is disarmed only too.)
//
// Runs on core 0 inside the web server, like every other handler.

#pragma once
#include <WebServer.h>
#include <Updater.h>
#include <LittleFS.h>
#include <StreamString.h>
#include "secrets.h"
#include "shared.h"
#include "telemetry.h"
#include "config.h"

#ifndef BB1_OTA_PASSWORD
#error "Add #define BB1_OTA_PASSWORD \"...\" to firmware/bb1/secrets.h (see secrets.example.h)"
#endif

// The LittleFS area must hold a whole firmware. FS_START / FS_END come from the
// board's Flash Size menu (-D flags), so a build with the wrong menu fails here
// instead of on BB1. flash.py and the CI build use the 1 MB / 1 MB split.
const uint32_t FLASH_FS_BYTES = 1024 * 1024;
static_assert(FS_END - FS_START >= FLASH_FS_BYTES,
              "OTA needs Flash Size 'Sketch: 1MB, FS: 1MB' (FQBN rp2040:rp2040:rpipicow:flash=2097152_1048576)");

const char *OTA_USER = "bb1";
const unsigned long OTA_SAFE_WAIT_MS = 100;    // how long core 0 waits for core 1 to confirm the motors are off

extern WebServer server;                       // web.h

struct OtaUpload {
  bool    ok = false;          // this upload was allowed to start and nothing has failed yet
  String  error;               // why it was refused or failed ("" = fine)
  size_t  bytes = 0;           // received so far
};
OtaUpload ota;

// The Updater's own words for what went wrong (it prints them; we keep them for the answer).
String otaUpdaterError() {
  StreamString s;
  Update.printError(s);
  s.trim();
  return s;
}

// Latest pitch and trim: lying down = more than FALL_ANGLE from upright.
bool otaLyingDown() {
  LogRecord r;
  uint32_t total = totalWritten;
  if (total == 0 || !logGet(total - 1, r)) return false;      // no reading yet: don't guess
  return fabs(r.pitch - balanceTrim) > FALL_ANGLE;
}

// Called once when the upload starts: every check, then hand core 1 the stop.
bool otaStart() {
  if (!server.authenticate(OTA_USER, BB1_OTA_PASSWORD)) { ota.error = "wrong password"; return false; }
  if (robotArmed)                                     { ota.error = "balancing: disarm and lay it down first"; return false; }
  if (motorTestRunning)                               { ota.error = "a motor test is running"; return false; }
  if (!otaLyingDown())                                { ota.error = "not lying down: after the restart it would start balancing"; return false; }
  otaActive = true;                                   // core 1: no arming, motors stay off
  unsigned long t0 = millis();
  while (!otaMotorsOff && millis() - t0 < OTA_SAFE_WAIT_MS) delay(1);   // core 1 answers within a tick (5 ms)
  if (!otaMotorsOff) { otaActive = false; ota.error = "core 1 didn't confirm the motors are off"; return false; }
  // The free space in the LittleFS area is the most a firmware may be.
  LittleFS.begin();                                   // formats the area the first time
  FSInfo fs;
  LittleFS.info(fs);
  if (!Update.begin(fs.totalBytes - fs.usedBytes, U_FLASH)) {
    ota.error = String("can't start: ") + otaUpdaterError();
    otaActive = false;
    return false;
  }
  return true;
}

// The WebServer calls this for every piece of the uploaded file.
void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    ota = OtaUpload();
    if (up.name != "firmware") ota.error = "send the file as form field 'firmware'";
    else ota.ok = otaStart();
    Serial.println(ota.ok ? "OTA: receiving " + up.filename : "OTA refused: " + ota.error);
  } else if (up.status == UPLOAD_FILE_WRITE && ota.ok) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      ota.ok = false;
      ota.error = String("write failed: ") + otaUpdaterError();
    }
    ota.bytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END && ota.ok) {
    if (!Update.end(true)) {                          // true: the size is what arrived
      ota.ok = false;
      ota.error = String("check failed: ") + otaUpdaterError();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    ota.ok = false;
    ota.error = "upload broke off";
  }
  if (!ota.ok && otaActive) {                         // failed after it started: back to normal
    Update.end(false);                                // drop the half file; no note for the bootloader
    otaActive = false;
  }
}

// Called once after the whole request: answer, then restart into the new firmware.
void handleUpdateDone() {
  char json[200];
  if (!ota.ok) {
    int code = ota.error == "wrong password" ? 401 : 409;
    if (code == 401) server.sendHeader("WWW-Authenticate", "Basic realm=\"bb1\"");
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", ota.error.length() ? ota.error.c_str() : "no file");
    server.send(code, "application/json", json);
    server.client().stop();
    ota = OtaUpload();                                // so the next request starts clean
    return;
  }
  snprintf(json, sizeof(json), "{\"ok\":true,\"bytes\":%u,\"note\":\"restarting into the new firmware\"}", (unsigned)ota.bytes);
  server.send(200, "application/json", json);
  server.client().stop();
  Serial.println("OTA: done, " + String(ota.bytes) + " bytes. Restarting.");
  delay(100);                                         // let the answer leave
  rp2040.restart();                                   // the bootloader copies it in, then starts it
}
