// version.h - which firmware this is, so we always know what's on BB1.
//
// Two parts:
//   FIRMWARE_VERSION  bumped by hand when a firmware change goes on BB1:
//                     "<milestone>.<change>", e.g. 3.2 = milestone 3, second
//                     firmware change in it (3.1 GET /api, 3.2 OTA updates,
//                     3.3 log buffer 8 KB smaller).
//                     The CHANGELOG entry for that change names the version.
//   BB1_BUILD_ID      written by tools/flash.py into build_id.h (gitignored)
//                     just before each compile: git commit, "+" if there were
//                     uncommitted changes, and the time. It tells two builds of
//                     the same version apart, and because the file changes every
//                     time, the sketch is always recompiled (arduino-cli would
//                     otherwise reuse its cached build, and the old compile time).
// Both show in GET /sys, on the main page (Pico memory panel) and in every log
// file's "# firmware_version" line. flash.py also adds a line to
// logs/flash_history.csv after every flash.

#pragma once

const char *FIRMWARE_VERSION = "3.3";

#if __has_include("build_id.h")
#include "build_id.h"
#endif
#ifndef BB1_BUILD_ID
#define BB1_BUILD_ID "no build id"   // built without flash.py (Arduino IDE, CI)
#endif
