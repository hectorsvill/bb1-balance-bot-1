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

// One line with every balance setting. Printed for the serial "gains" command
// and whenever the web page changes them, so tools/handheld.py always shows
// (and logs) the gains BB1 is really using. handheld.py reads this exact layout.
void printGains() {
  Serial.println("gains v" + String(gainsVersion) + ": kp " + String(gainKp, 2) + "  ki " + String(gainKi, 2) +
                 "  kd " + String(gainKd, 2) + "  trim " + String(balanceTrim, 2) + " deg  cap " + String(pwmCap, 0) +
                 "  deadband " + String(motorDeadband, 0));
}

const int      SETTINGS_ADDR  = 128;
const uint32_t SETTINGS_MAGIC = 0xBB1C0001;   // "this really is our data", and its layout version

struct SavedSettings {
  uint32_t magic;
  float kp, ki, kd, trim, cap, deadband;
};

// Called once on core 0 after wifiStart() (which opens the EEPROM area).
// Returns true if saved settings were found and used.
bool settingsLoad() {
  SavedSettings s;
  EEPROM.get(SETTINGS_ADDR, s);
  if (s.magic != SETTINGS_MAGIC) return false;          // never saved: keep the config.h defaults
  // Same limits as the serial commands, in case the flash holds something odd.
  gainKp = constrain(s.kp, 0.0f, 100.0f);
  gainKi = constrain(s.ki, 0.0f, 100.0f);
  gainKd = constrain(s.kd, 0.0f, 20.0f);
  balanceTrim = constrain(s.trim, -20.0f, 20.0f);
  pwmCap = constrain(s.cap, 0.0f, (float)PWM_MAX);
  motorDeadband = constrain(s.deadband, 0.0f, 100.0f);
  gainsVersion = gainsVersion + 1;
  return true;
}

// Save what's in use now. Returns an explanation for the page / serial.
String settingsSave() {
  if (robotArmed || motorTestRunning) return "disarm first: saving pauses the control loop for a moment";
  SavedSettings s = { SETTINGS_MAGIC, gainKp, gainKi, gainKd, balanceTrim, pwmCap, motorDeadband };
  EEPROM.put(SETTINGS_ADDR, s);
  EEPROM.commit();                                       // the actual flash write
  return "saved: kp " + String(gainKp, 2) + ", ki " + String(gainKi, 2) + ", kd " + String(gainKd, 2) +
         ", trim " + String(balanceTrim, 2) + ", cap " + String(pwmCap, 0) + ", deadband " + String(motorDeadband, 0);
}

// Back to the config.h defaults (in use now; Save to keep them).
void settingsDefaults() {
  gainKp = DEFAULT_KP; gainKi = DEFAULT_KI; gainKd = DEFAULT_KD;
  balanceTrim = DEFAULT_TRIM; pwmCap = DEFAULT_PWM_CAP; motorDeadband = DEFAULT_DEADBAND;
  gainsVersion = gainsVersion + 1;
}
