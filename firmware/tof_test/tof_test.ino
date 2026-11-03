// tof_test.ino - Self-Balancing Robot, test 3: VL53L0X time-of-flight distance sensor
//
// What it does: scans I2C1 (GP6/GP7) for anything that answers, then starts the
// VL53L0X and prints the distance straight ahead about 20 times a second, with
// the closest and farthest it has seen. Wave a hand in front of it: the number
// should follow your hand from about 3 cm out to 1-1.2 m.
//
// How it measures: it fires a short pulse of invisible 940 nm laser light and
// times how long the reflection takes to come back. Light travels 30 cm per
// nanosecond, so the chip is timing picoseconds; it does that with an array of
// single-photon detectors (SPADs) and averages many pulses per reading.
// Quad picture: like the ToF "rangefinder" some FPV builds use for altitude hold.
//
// Why I2C1 and not the IMU's bus (I2C0, GP4/GP5): in bb1, core 1 reads the IMU
// 200 times a second and must never wait. The VL53L0X is slow (a reading takes
// about 33 ms), so it gets its own bus, shared later with the SSD1306 OLED
// (0x3C; the VL53L0X is 0x29, so they don't clash).
//
// Library: Pololu VL53L0X 1.3.1 (arduino-cli lib install "VL53L0X@1.3.1").
// Board: Raspberry Pi Pico W, "Raspberry Pi Pico/RP2040" core by Earle Philhower.
// Serial monitor: 115200 baud. USB alone powers it. The motor driver is kept
// asleep (STBY low) the whole time, so the wheels can't move.

#include <Wire.h>
#include <VL53L0X.h>

// ---------- wiring (docs/WIRING.md, docs/FUTURE_INTEGRATIONS.md) ----------
const int PIN_SDA1  = 6;        // Pico pin 9  -> sensor SDA
const int PIN_SCL1  = 7;        // Pico pin 10 -> sensor SCL
                                // sensor VIN -> 3V3(OUT) pin 36, GND -> any GND
const int PIN_STBY  = 9;        // D153C STBY: LOW = motor driver asleep
const uint8_t TOF_ADDR = 0x29;  // the VL53L0X's address out of the box

VL53L0X tof;
uint16_t closest = 65535, farthest = 0;

// Who answers on I2C1? Prints every address that acks. Expect 0x29.
void scanBus() {
  Serial.println("Scanning I2C1 (GP6 SDA, GP7 SCL)...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0) {     // 0 = the device acknowledged its address
      Serial.print("  found 0x");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (!found) Serial.println("  nothing. Check VIN (3V3, pin 36), GND, SDA on GP6 (pin 9), SCL on GP7 (pin 10).");
}

void setup() {
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);              // motors can't run during this test

  Serial.begin(115200);
  while (!Serial && millis() < 5000) {}     // wait up to 5 s for the monitor
  Serial.println("\ntof_test: VL53L0X on I2C1");

  Wire1.setSDA(PIN_SDA1);
  Wire1.setSCL(PIN_SCL1);
  Wire1.begin();
  Wire1.setClock(400000);                   // 400 kHz, like the IMU bus

  scanBus();

  tof.setBus(&Wire1);                       // the library defaults to Wire (I2C0): point it at I2C1
  tof.setTimeout(500);                      // ms: a read that takes longer counts as failed
  if (!tof.init()) {
    Serial.println("! VL53L0X didn't start (no answer at 0x29, or not a VL53L0X). Stopping here.");
    while (true) delay(1000);
  }
  // Timing budget: how long it averages for one reading. Longer = steadier and
  // farther, but fewer readings a second. 33 ms is the chip's default (about 30
  // readings a second); 20 ms is the fastest that still works well.
  tof.setMeasurementTimingBudget(33000);
  tof.startContinuous();                    // measure back to back, no waiting between reads
  Serial.println("Reading. Columns: distance mm, closest, farthest. 8190+ = nothing in range.");
}

void loop() {
  uint16_t mm = tof.readRangeContinuousMillimeters();
  if (tof.timeoutOccurred()) {
    Serial.println("timeout (no reading in 500 ms)");
    return;
  }
  // About 8190 mm means "no target": nothing reflected enough light back.
  bool valid = mm < 8000;
  if (valid) {
    if (mm < closest)  closest = mm;
    if (mm > farthest) farthest = mm;
  }
  Serial.print(valid ? String(mm) : String("out of range"));
  Serial.print("\t");
  Serial.print(closest == 65535 ? 0 : closest);
  Serial.print("\t");
  Serial.println(farthest);
  delay(50);                                // print about 20 times a second
}
