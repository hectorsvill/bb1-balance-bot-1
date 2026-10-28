// api.h - GET /api: a list of every endpoint and its limits, as JSON.
//
// So a program (or an AI agent) can find out what BB1 understands without
// reading docs/API.md. The table below is the list; the limits come straight
// from config.h, so they can't go stale when a limit changes.
//
// Adding an endpoint: route it in webBegin() (web.h), add a row here, a fake
// answer in tools/mock_bb1.py, a row in docs/API.md, and check docs/AGENT.md
// (bump API_VERSION and AGENT.md's version line together; CI compares them).

#pragma once
#include "config.h"
#include "log_meta.h"

// API_VERSION counts changes to this list (new endpoint, new argument, new
// limit), so an agent can tell when to re-read it. 1 = first version,
// 2 = POST /update (ota.h), 3 = version and build_id (version.h).
const int API_VERSION = 3;

struct ApiEndpoint {
  const char *method;   // "GET" or "POST"
  const char *path;
  const char *args;     // argument names, comma separated ("" = none)
  const char *answer;   // "json", "text", "csv" or "html"
  bool moves;           // true = can turn the wheels
  const char *does;     // one line for a person (or an agent) to read
};

const ApiEndpoint API_ENDPOINTS[] = {
  {"GET",  "/api",            "",                  "json", false, "this list"},
  {"GET",  "/state",          "",                  "json", false, "everything now; also the heartbeat for heartbeat arms"},
  {"GET",  "/log",            "since",             "csv",  false, "every 5 ms record from tick since on (max log_max_per_request)"},
  {"GET",  "/log/meta",       "",                  "text", false, "the # setting lines for the top of a log file"},
  {"GET",  "/sys",            "",                  "json", false, "RAM and flash use"},
  {"GET",  "/wifi",           "",                  "json", false, "network mode and saved home network name"},
  {"POST", "/arm",            "seconds,heartbeat", "text", true,  "start balancing; refused unless within arm_angle of trim; result in /state armed, reason"},
  {"POST", "/disarm",         "why",               "text", false, "stop balancing; auto-arm off until the next arm"},
  {"POST", "/drive",          "speed,turn,ms",     "json", true,  "drive while balancing; runs out after ms, then ramps to a stop"},
  {"POST", "/drive/stop",     "",                  "json", false, "ramp to a stop now, keep balancing"},
  {"POST", "/motor/stop",     "",                  "text", false, "STOP: end a motor test and disarm"},
  {"POST", "/step",           "",                  "text", true,  "step test while armed: setpoint +2 deg for 1 s"},
  {"POST", "/gains",          "kp,ki,kd,trim,cap,deadband,sp,si,th,auto,dff,sdi,smode,wsp,wsi,irelax", "text", false, "change settings (see gains for limits)"},
  {"POST", "/gains/save",     "",                  "text", false, "store settings in flash (disarmed only)"},
  {"POST", "/gains/defaults", "",                  "text", false, "back to the config.h defaults"},
  {"POST", "/alpha",          "value",             "text", false, "angle filter weight"},
  {"POST", "/cal",            "",                  "text", false, "recalibrate the gyro (2 s, keep still; disarms)"},
  {"POST", "/motor",          "left,right,ms",     "text", true,  "timed motor test, disarmed only, wheels in the air"},
  {"POST", "/wifi",           "ssid,password",     "text", false, "save a home network and restart"},
  {"POST", "/wifi/forget",    "",                  "text", false, "forget the home network and restart"},
  {"POST", "/update",         "firmware",          "json", false, "new firmware over Wi-Fi (multipart file, Basic auth bb1); lying down and disarmed only; restarts"},
  {"GET",  "/",               "",                  "html", false, "the main page"},
  {"GET",  "/tune",           "",                  "html", false, "the PID tuning page"},
};

// One "name":[min,max] pair for the gains object.
String apiRange(const char *name, float lo, float hi) {
  return "\"" + String(name) + "\":[" + String(lo, 3) + "," + String(hi, 3) + "]";
}

// "speed,turn,ms" -> ["speed","turn","ms"]
String apiArgs(const char *args) {
  String out = "[", a = args;
  int start = 0;
  while (start < (int)a.length()) {
    int comma = a.indexOf(',', start);
    if (comma < 0) comma = a.length();
    if (start) out += ",";
    out += "\"" + a.substring(start, comma) + "\"";
    start = comma + 1;
  }
  return out + "]";
}

// The whole answer. About 4 KB; built once per request (agents ask rarely).
// logMax is LOG_MAX_PER_REQUEST from web.h.
String apiJson(uint32_t logMax) {
  String j;
  j.reserve(4600);                              // one allocation instead of many small ones
  j += "{\"api\":" + String(API_VERSION) + ",\"firmware\":\"" + FIRMWARE_NAME + "\",\"version\":\"" + FIRMWARE_VERSION + "\",\"build_id\":\"" + BB1_BUILD_ID +
       "\",\"log_format\":" + String(LOG_FORMAT) + ",";
  j += "\"units\":{\"angle\":\"deg (forward +)\",\"speed\":\"wheel turns/s (forward +)\",\"turn\":\"wheel turns/s left minus right (right +)\","
       "\"pwm\":\"-255..255 (forward +)\",\"counts_per_rev\":" + String(COUNTS_PER_REV) + "},";
  j += "\"limits\":{\"drive_max_speed\":" + String(DRIVE_MAX_SPEED, 2) + ",\"drive_max_turn\":" + String(DRIVE_MAX_TURN, 2) +
       ",\"drive_hold_ms\":" + String(DRIVE_HOLD_MS) + ",\"drive_hold_max_ms\":" + String(DRIVE_HOLD_MAX_MS) +
       ",\"arm_seconds_max\":" + String(ARM_SECONDS_MAX, 0) + ",\"arm_angle\":" + String(ARM_ANGLE, 1) + ",\"fall_angle\":" + String(FALL_ANGLE, 1) +
       ",\"heartbeat_ms\":" + String(HEARTBEAT_MS) + ",\"motor_test_max_pwm\":" + String(MOTOR_TEST_MAX_PWM) +
       ",\"motor_test_max_ms\":" + String(MOTOR_TEST_MAX_MS) + ",\"alpha\":[0.900,0.999],\"log_max_per_request\":" + String(logMax) + "},";
  // Same limits setBalanceSetting() (settings.h) clamps to. auto and smode are 0 or 1; trim also takes "here".
  j += "\"gains\":{" + apiRange("kp", 0, GAIN_P_MAX) + "," + apiRange("ki", 0, GAIN_I_MAX) + "," + apiRange("kd", 0, GAIN_D_MAX) + "," +
       apiRange("trim", -TRIM_MAX, TRIM_MAX) + "," + apiRange("cap", 0, PWM_MAX) + "," + apiRange("deadband", 0, DEADBAND_MAX) + "," +
       apiRange("sp", 0, SPEED_GAIN_MAX) + "," + apiRange("si", 0, SPEED_GAIN_MAX) + "," + apiRange("th", 0, TURN_HOLD_GAIN_MAX) + "," +
       apiRange("auto", 0, 1) + "," + apiRange("dff", 0, DRIVE_FF_MAX) + "," + apiRange("sdi", 0, DRIVE_SPEED_I_MAX) + "," +
       apiRange("smode", 0, 1) + "," + apiRange("wsp", 0, WHEEL_SPEED_GAIN_MAX) + "," + apiRange("wsi", 0, WHEEL_SPEED_GAIN_MAX) + "," +
       apiRange("irelax", 0, I_RELAX_MAX_S) + "},";
  j += "\"endpoints\":[";
  const int n = sizeof(API_ENDPOINTS) / sizeof(API_ENDPOINTS[0]);
  for (int k = 0; k < n; k++) {
    const ApiEndpoint &e = API_ENDPOINTS[k];
    if (k) j += ",";
    j += "{\"method\":\"" + String(e.method) + "\",\"path\":\"" + e.path + "\",\"args\":" + apiArgs(e.args) +
         ",\"answer\":\"" + e.answer + "\",\"moves\":" + (e.moves ? "true" : "false") + ",\"does\":\"" + e.does + "\"}";
  }
  j += "]}";
  return j;
}
