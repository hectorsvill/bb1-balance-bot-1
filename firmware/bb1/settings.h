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
  else return false;
  return true;
}

// Every balance setting on one line. Serial prints it ("gains" and after any
// change, so tools/handheld.py shows and logs what BB1 really uses: it reads
// this exact layout), and the web pages send it back as their answer.
String gainsLine() {
  return "gains v" + String(gainsVersion) + ": kp " + String(gainP, 2) + "  ki " + String(gainI, 2) +
         "  kd " + String(gainD, 2) + "  trim " + String(balanceTrim, 2) + " deg  cap " + String(pwmCap, 0) +
         "  deadband " + String(motorDeadband, 0) + "  sp " + String(gainSpeedP, 2) + "  si " + String(gainSpeedI, 2);
}

void printGains() {
  Serial.println(gainsLine());
}

const int      SETTINGS_ADDR  = 128;
const uint32_t SETTINGS_MAGIC    = 0xBB1C0002;   // "this really is our data", and its layout version
const uint32_t SETTINGS_MAGIC_V1 = 0xBB1C0001;   // before step 8: no speed loop gains (still loads)

struct SavedSettings {
  uint32_t magic;
  float gainP, gainI, gainD, trim, cap, deadband;
  float speedP, speedI;                                  // step 8 (layout v2)
};

// Called once on core 0 after wifiStart() (which opens the EEPROM area).
// Returns true if saved settings were found and used.
bool settingsLoad() {
  SavedSettings s;
  EEPROM.get(SETTINGS_ADDR, s);
  if (s.magic != SETTINGS_MAGIC && s.magic != SETTINGS_MAGIC_V1) return false;   // never saved: keep the config.h defaults
  // Through the same limits as serial and the page, in case the flash holds something odd.
  setBalanceSetting("kp", String(s.gainP, 4));
  setBalanceSetting("ki", String(s.gainI, 4));
  setBalanceSetting("kd", String(s.gainD, 4));
  setBalanceSetting("trim", String(s.trim, 4));
  setBalanceSetting("cap", String(s.cap, 4));
  setBalanceSetting("deadband", String(s.deadband, 4));
  if (s.magic == SETTINGS_MAGIC) {                       // saved with step 8; saved before that = speed loop stays at its default (off)
    setBalanceSetting("sp", String(s.speedP, 4));
    setBalanceSetting("si", String(s.speedI, 4));
  }
  gainsVersion = gainsVersion + 1;
  return true;
}

// Save what's in use now. Returns an explanation for the page / serial.
String settingsSave() {
  if (robotArmed || motorTestRunning) return "disarm first: saving pauses the control loop for a moment";
  SavedSettings s = { SETTINGS_MAGIC, gainP, gainI, gainD, balanceTrim, pwmCap, motorDeadband, gainSpeedP, gainSpeedI };
  EEPROM.put(SETTINGS_ADDR, s);
  EEPROM.commit();                                       // the actual flash write
  return "saved: " + gainsLine();
}

// Back to the config.h defaults (in use now; Save to keep them).
void settingsDefaults() {
  gainP = DEFAULT_P_GAIN; gainI = DEFAULT_I_GAIN; gainD = DEFAULT_D_GAIN;
  balanceTrim = DEFAULT_TRIM; pwmCap = DEFAULT_PWM_CAP; motorDeadband = DEFAULT_DEADBAND;
  gainSpeedP = DEFAULT_SPEED_P; gainSpeedI = DEFAULT_SPEED_I;
  gainsVersion = gainsVersion + 1;
}
