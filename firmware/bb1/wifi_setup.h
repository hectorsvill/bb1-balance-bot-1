// wifi_setup.h - own network or home network, and the saved settings.
//
// Two ways to reach BB1:
//   own network (default)   BB1 makes a Wi-Fi network called "BB1" (an access point, "AP").
//                           Join it, then open http://192.168.42.1
//   home network            BB1 joins your Wi-Fi as a normal device (a "station", "STA").
//                           Open http://bb1.local (or the IP printed on serial).
//
// The home network's name and password are saved in the Pico's flash (the
// EEPROM library: a small area at the end of flash), so they survive power-off
// and re-flashing. If the home network isn't found within WIFI_JOIN_TIMEOUT_MS,
// BB1 falls back to its own network, so you can always get back in.
//
// Runs on core 0 only. Saving to flash pauses core 1 for a moment, so later
// steps only allow it while the motors are disarmed.

#pragma once
#include <WiFi.h>
#include <EEPROM.h>
#include <LEAmDNS.h>
#include "secrets.h"

const char *BB1_HOSTNAME = "bb1";                 // -> http://bb1.local on the home network
const unsigned long WIFI_JOIN_TIMEOUT_MS = 15000;

// What we keep in flash. "magic" is a known number that tells us the
// area holds our settings and not leftover random bytes.
const uint32_t WIFI_SETTINGS_MAGIC = 0xBB1F0001;
struct WifiSettings {
  uint32_t magic;
  char ssid[33];            // Wi-Fi names are at most 32 characters (+1 for the ending 0)
  char password[64];        // WPA2 passwords are at most 63
};

WifiSettings wifiSettings;

// Current state, for the page and the serial readout
enum WifiMode { WIFI_OWN_NETWORK, WIFI_HOME_NETWORK };
WifiMode wifiMode = WIFI_OWN_NETWORK;
String wifiNote = "";       // e.g. "home network not found, using own network"

bool homeNetworkSaved() {
  return wifiSettings.magic == WIFI_SETTINGS_MAGIC && wifiSettings.ssid[0] != 0;
}

void wifiLoadSettings() {
  EEPROM.begin(256);        // reserve 256 bytes of flash-backed storage (we use about 100)
  EEPROM.get(0, wifiSettings);
  if (wifiSettings.magic != WIFI_SETTINGS_MAGIC) memset(&wifiSettings, 0, sizeof(wifiSettings));
  // Make sure the strings end, even if flash held junk.
  wifiSettings.ssid[32] = 0;
  wifiSettings.password[63] = 0;
}

// Save a home network. Takes effect after a restart.
void wifiSaveHomeNetwork(const String &ssid, const String &password) {
  memset(&wifiSettings, 0, sizeof(wifiSettings));
  wifiSettings.magic = WIFI_SETTINGS_MAGIC;
  ssid.toCharArray(wifiSettings.ssid, sizeof(wifiSettings.ssid));
  password.toCharArray(wifiSettings.password, sizeof(wifiSettings.password));
  EEPROM.put(0, wifiSettings);
  EEPROM.commit();          // the actual flash write
}

// Forget the home network: next start uses the own network.
void wifiForgetHomeNetwork() {
  memset(&wifiSettings, 0, sizeof(wifiSettings));
  EEPROM.put(0, wifiSettings);
  EEPROM.commit();
}

void startOwnNetwork() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 42, 1), IPAddress(192, 168, 42, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(BB1_AP_SSID, BB1_AP_PASSWORD);
  wifiMode = WIFI_OWN_NETWORK;
}

// Try the saved home network; fall back to the own network.
// Blocks core 0 for up to 15 s while joining. Core 1 keeps running meanwhile.
void wifiStart() {
  wifiLoadSettings();
  WiFi.setHostname(BB1_HOSTNAME);

  if (homeNetworkSaved()) {
    Serial.print("Joining home network \"");
    Serial.print(wifiSettings.ssid);
    Serial.print("\"");
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSettings.ssid, wifiSettings.password);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) {
      delay(500);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      wifiMode = WIFI_HOME_NETWORK;
      MDNS.begin(BB1_HOSTNAME);               // answers to "bb1.local"
      MDNS.addService("http", "tcp", 80);
      return;
    }
    wifiNote = "home network \"" + String(wifiSettings.ssid) + "\" not found, using own network";
  }
  startOwnNetwork();
}

// Call from loop() on core 0.
void wifiLoop() {
  if (wifiMode == WIFI_HOME_NETWORK) MDNS.update();
}

String wifiAddress() {
  return wifiMode == WIFI_HOME_NETWORK ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

void wifiPrintStatus() {
  if (wifiMode == WIFI_HOME_NETWORK) {
    Serial.println("Wi-Fi: home network \"" + String(wifiSettings.ssid) + "\", open http://" +
                   String(BB1_HOSTNAME) + ".local or http://" + wifiAddress());
  } else {
    Serial.println("Wi-Fi: own network \"" + String(BB1_AP_SSID) + "\" (password in secrets.h), open http://" +
                   wifiAddress());
  }
  if (wifiNote.length()) Serial.println("  note: " + wifiNote);
}
