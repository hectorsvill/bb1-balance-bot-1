// settings.h - save the balance settings (gains, trim, cap, deadband) in flash.
//
// Same flash area as the home Wi-Fi settings (the EEPROM library, see
// wifi_setup.h): Wi-Fi uses bytes 0..127, the balance settings live at
// SETTINGS_ADDR. At startup they replace the defaults from config.h, so a
// restart no longer forgets your tuning.
//
// Writing flash pauses core 1 for a few milliseconds (the program runs from
// that same flash chip). That's harmless while disarmed, dangerous while
// balancing, so settingsSave() refuses unless the motors are off.

#pragma once
#include <EEPROM.h>
#include "config.h"
#include "shared.h"
#include "telemetry.h"

// ---------- one place for the balance settings ----------
// Serial ("kp 15"), the web pages (POST /gains kp=15) and loading from flash
// all go through setBalanceSetting(), so the names and limits live here only.

// The newest pitch, read from the log like everything else on core 0 ("trim here").
float latestPitch() {
  LogRecord r;
  return (totalWritten && logGet(totalWritten - 1, r)) ? r.pitch : 0;
}

// Set one setting by name, within its limits. "p"/"i"/"d" work like "kp"/"ki"/"kd"
// (Betaflight says P, textbooks say Kp). value "here" for trim = the current pitch.
// Returns false for an unknown name. Doesn't bump gainsVersion: the caller does,
// once, after changing everything it was asked to.
bool setBalanceSetting(String name, const String &value) {
  float v = value.toFloat();
  if      (name == "kp" || name == "p") gainP = constrain(v, 0.0f, GAIN_P_MAX);
  else if (name == "ki" || name == "i") gainI = constrain(v, 0.0f, GAIN_I_MAX);
  else if (name == "kd" || name == "d") gainD = constrain(v, 0.0f, GAIN_D_MAX);
  else if (name == "trim")              balanceTrim = (value == "here") ? latestPitch() : constrain(v, -TRIM_MAX, TRIM_MAX);
  else if (name == "cap")               pwmCap = constrain(v, 0.0f, (float)PWM_MAX);
  else if (name == "deadband")          motorDeadband = constrain(v, 0.0f, DEADBAND_MAX);
  else if (name == "sp")                gainSpeedP = constrain(v, 0.0f, SPEED_GAIN_MAX);   // speed loop P
  else if (name == "si")                gainSpeedI = constrain(v, 0.0f, SPEED_GAIN_MAX);   // speed loop I (position)
  else if (name == "th")                gainTurnHold = constrain(v, 0.0f, TURN_HOLD_GAIN_MAX);   // heading hold
  else if (name == "dff")               gainDriveFF = constrain(v, 0.0f, DRIVE_FF_MAX);         // drive feedforward
  else if (name == "sdi")               gainDriveSpeedI = constrain(v, 0.0f, DRIVE_SPEED_I_MAX); // drive speed I
  else if (name == "smode")             speedMode = v >= 0.5f ? 1 : 0;                           // speed loop mode (config.h)
  else if (name == "wsp")               gainWheelSpeedP = constrain(v, 0.0f, WHEEL_SPEED_GAIN_MAX); // smode 1 wheel speed P
  else if (name == "wsi")               gainWheelSpeedI = constrain(v, 0.0f, WHEEL_SPEED_GAIN_MAX); // smode 1 wheel speed I
  else if (name == "auto")              autoArm = (v >= 0.5f) ? 1 : 0;                         // auto-arm on / off
  else return false;
  return true;
}

// Every balance setting on one line. Serial prints it ("gains" and after any
// change, so tools/handheld.py shows and logs what BB1 really uses: it reads
// this exact layout), and the web pages send it back as their answer.
String gainsLine() {
  return "gains v" + String(gainsVersion) + ": kp " + String(gainP, 2) + "  ki " + String(gainI, 2) +
         "  kd " + String(gainD, 2) + "  trim " + String(balanceTrim, 2) + " deg  cap " + String(pwmCap, 0) +
         "  deadband " + String(motorDeadband, 0) + "  sp " + String(gainSpeedP, 2) + "  si " + String(gainSpeedI, 2) + "  th " + String(gainTurnHold, 1) + "  auto " + String((int)autoArm) + "  dff " + String(gainDriveFF, 0) + "  sdi " + String(gainDriveSpeedI, 2) +
         "  smode " + String((int)speedMode) + "  wsp " + String(gainWheelSpeedP, 1) + "  wsi " + String(gainWheelSpeedI, 1);
}

void printGains() {
  Serial.println(gainsLine());
}

const int      SETTINGS_ADDR  = 128;
// "This really is our data", and its layout version. Fields are only ever
// added at the end, so an older layout is just a shorter prefix and still loads.
const uint32_t SETTINGS_MAGIC    = 0xBB1C0007;   // v7: + speed loop mode, wheel speed P and I
const uint32_t SETTINGS_MAGIC_V6 = 0xBB1C0006;   // v6: + drive speed I
const uint32_t SETTINGS_MAGIC_V5 = 0xBB1C0005;   // v5: + drive feedforward
const uint32_t SETTINGS_MAGIC_V4 = 0xBB1C0004;   // v4: + auto-arm
const uint32_t SETTINGS_MAGIC_V3 = 0xBB1C0003;   // v3: + heading hold
const uint32_t SETTINGS_MAGIC_V2 = 0xBB1C0002;   // v2: + speed loop gains
const uint32_t SETTINGS_MAGIC_V1 = 0xBB1C0001;   // v1: gains, trim, cap, deadband

struct SavedSettings {
  uint32_t magic;
  float gainP, gainI, gainD, trim, cap, deadband;
  float speedP, speedI;                                  // step 8 (layout v2)
  float turnHold;                                        // heading hold (layout v3)
  float autoArm;                                         // auto-arm on / off (layout v4)
  float driveFF;                                         // drive feedforward (layout v5)
  float driveSpeedI;                                     // drive speed I (layout v6)
  float speedMode, wheelSpeedP, wheelSpeedI;             // speed loop mode and the wheel speed loop (layout v7)
};

// Called once on core 0 after wifiStart() (which opens the EEPROM area).
// Returns true if saved settings were found and used.
bool settingsLoad() {
  SavedSettings s;
  EEPROM.get(SETTINGS_ADDR, s);
  int version = s.magic == SETTINGS_MAGIC ? 7 : s.magic == SETTINGS_MAGIC_V6 ? 6 : s.magic == SETTINGS_MAGIC_V5 ? 5 : s.magic == SETTINGS_MAGIC_V4 ? 4 : s.magic == SETTINGS_MAGIC_V3 ? 3 :
                s.magic == SETTINGS_MAGIC_V2 ? 2 : s.magic == SETTINGS_MAGIC_V1 ? 1 : 0;
  if (version == 0) return false;                       // never saved: keep the config.h defaults
  // Through the same limits as serial and the page, in case the flash holds something odd.
  setBalanceSetting("kp", String(s.gainP, 4));
  setBalanceSetting("ki", String(s.gainI, 4));
  setBalanceSetting("kd", String(s.gainD, 4));
  setBalanceSetting("trim", String(s.trim, 4));
  setBalanceSetting("cap", String(s.cap, 4));
  setBalanceSetting("deadband", String(s.deadband, 4));
  if (version >= 2) {                                   // saved before step 8: speed loop stays at its default (off)
    setBalanceSetting("sp", String(s.speedP, 4));
    setBalanceSetting("si", String(s.speedI, 4));
  }
  if (version >= 3) setBalanceSetting("th", String(s.turnHold, 4));
  if (version >= 4) setBalanceSetting("auto", String(s.autoArm, 0));
  if (version >= 5) setBalanceSetting("dff", String(s.driveFF, 4));   // older saves keep the config.h default
  if (version >= 6) setBalanceSetting("sdi", String(s.driveSpeedI, 4));
  if (version >= 7) {
    setBalanceSetting("smode", String(s.speedMode, 0));
    setBalanceSetting("wsp", String(s.wheelSpeedP, 4));
    setBalanceSetting("wsi", String(s.wheelSpeedI, 4));
  }
  gainsVersion = gainsVersion + 1;
  return true;
}

// Save what's in use now. Returns an explanation for the page / serial.
String settingsSave() {
  if (robotArmed || motorTestRunning) return "disarm first: saving pauses the control loop for a moment";
  SavedSettings s = { SETTINGS_MAGIC, gainP, gainI, gainD, balanceTrim, pwmCap, motorDeadband, gainSpeedP, gainSpeedI, gainTurnHold, autoArm, gainDriveFF, gainDriveSpeedI, speedMode, gainWheelSpeedP, gainWheelSpeedI };
  EEPROM.put(SETTINGS_ADDR, s);
  EEPROM.commit();                                       // the actual flash write
  return "saved: " + gainsLine();
}

// Back to the config.h defaults (in use now; Save to keep them).
void settingsDefaults() {
  gainP = DEFAULT_P_GAIN; gainI = DEFAULT_I_GAIN; gainD = DEFAULT_D_GAIN;
  balanceTrim = DEFAULT_TRIM; pwmCap = DEFAULT_PWM_CAP; motorDeadband = DEFAULT_DEADBAND;
  gainSpeedP = DEFAULT_SPEED_P; gainSpeedI = DEFAULT_SPEED_I; gainTurnHold = DEFAULT_TURN_HOLD; autoArm = DEFAULT_AUTO_ARM;
  gainDriveFF = DEFAULT_DRIVE_FF;
  gainDriveSpeedI = DEFAULT_DRIVE_SPEED_I;
  speedMode = DEFAULT_SPEED_MODE;
  gainWheelSpeedP = DEFAULT_WHEEL_SPEED_P;
  gainWheelSpeedI = DEFAULT_WHEEL_SPEED_I;
  gainsVersion = gainsVersion + 1;
}
