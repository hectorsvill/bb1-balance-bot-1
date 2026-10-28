// secrets.example.h - template for secrets.h.
//
// secrets.h holds passwords, so git ignores it. To build on a new machine:
//   cp firmware/bb1/secrets.example.h firmware/bb1/secrets.h
// then edit secrets.h.
//
// The HOME Wi-Fi name and password are NOT kept here: you type them into the
// web page's Network panel and the Pico saves them in its own flash.

#pragma once

// The robot's own network (access point). WPA2 needs at least 8 characters.
#define BB1_AP_SSID     "BB1"
#define BB1_AP_PASSWORD "change-me-please"   // pick your own (8+ characters) in secrets.h:
                                             // anyone who joins this network can drive the robot

// Firmware updates over Wi-Fi (ota.h, tools/flash.py --ota): POST /update asks
// for this password (user "bb1"). Anyone with it can replace BB1's firmware.
#define BB1_OTA_PASSWORD "change-me-too"
