// battery.h - the 2S LiPo voltage, read on core 0.
//
// The D153C already divides its input (the battery, after its power switch)
// by 11 with a 10 k / 1 k pair and puts it on the "ADC" pin of its Output_IO
// header (H2 pin 5; docs/KIT_REFERENCE.md). That pin is wired to GP26 (ADC0).
//
//   battery 8.4 V full -> 0.76 V on GP26     6.0 V empty -> 0.55 V
//
// Why core 0: core 1 runs the 200 Hz loop and never waits on anything slow. An
// ADC read takes a few microseconds, but averaging 16 of them every 100 ms is
// work core 1 doesn't need. Core 0 stores the result in batteryMv (shared.h);
// core 1 copies it into every log record, so the logs show the voltage each
// tick (the same PWM pushes less as the battery drops: a model needs to know).
//
// Nothing else uses the ADC. GP26 isn't shared with Wi-Fi (only GP29 is on the
// Pico W).

#pragma once
#include "config.h"
#include "shared.h"

void batteryBegin() {
  analogReadResolution(12);                    // 0..4095 for 0..3.3 V
  pinMode(BATTERY_PIN, INPUT);
  // Right after power-up the first ADC readings came out low (5.2 V for a
  // 7.35 V pack, 2026-10-24). Throw a few away and let the ADC settle.
  for (int n = 0; n < 16; n++) analogRead(BATTERY_PIN);
  delay(5);
}

// Call from loop() on core 0. Reads every BATTERY_READ_MS, averages 16
// samples, then smooths a little so the motors' current spikes don't make
// the number jump around.
void batteryUpdate() {
  static unsigned long last = 0;
  static float smoothed = -1;                  // -1 = no reading yet
  if (smoothed >= 0 && millis() - last < BATTERY_READ_MS) return;   // the very first call always reads
  last = millis();

  uint32_t sum = 0;
  for (int n = 0; n < 16; n++) sum += analogRead(BATTERY_PIN);
  float volts = (sum / 16.0f) / 4095.0f * 3.3f * BATTERY_DIVIDER;

  smoothed = (smoothed < 0) ? volts : smoothed + 0.2f * (volts - smoothed);
  batteryMv = (uint16_t)constrain(smoothed * 1000.0f, 0.0f, 65535.0f);
}

// Under BATTERY_MIN_SENSE_V the reading means "no battery wire / switch off".
bool batteryPresent() {
  return batteryMv >= BATTERY_MIN_SENSE_V * 1000;
}
