// imu_test.ino - Self-Balancing Robot, test 2: MPU-6050 (GY-521)
//
// What it does: finds the MPU-6050 on the I2C bus, checks its ID, wakes it up,
// then prints acceleration, rotation speed, tilt angles and temperature 10 times a second.
// Talks to the chip's registers directly (no library) so you can see how it works.
//
// Board: Raspberry Pi Pico W, "Raspberry Pi Pico/RP2040" core by Earle Philhower.
// Serial monitor: 115200 baud. Until the 1N5819 is in, switch the D153C on before plugging in USB.

#include <Wire.h>

// ---------- wiring (same as the Connections sheet) ----------
const int PIN_SDA = 4;          // Pico pin 6
const int PIN_SCL = 5;          // Pico pin 7
const uint8_t MPU_ADDR = 0x68;  // AD0 left open -> address 0x68

// ---------- MPU-6050 registers we use ----------
// A register is a numbered memory cell inside the chip. Writing one changes a setting;
// reading one gives you data.
const uint8_t REG_GYRO_CONFIG  = 0x1B;  // gyro range
const uint8_t REG_ACCEL_CONFIG = 0x1C;  // accelerometer range
const uint8_t REG_ACCEL_XOUT_H = 0x3B;  // first of 14 data bytes: accel x,y,z, temp, gyro x,y,z
const uint8_t REG_PWR_MGMT_1   = 0x6B;  // power: the chip starts asleep
const uint8_t REG_WHO_AM_I     = 0x75;  // always reads 0x68 on a real MPU-6050

// Scale factors for the ranges set in setup() (from the MPU-6050 datasheet)
const float ACCEL_PER_G   = 16384.0;    // +/-2 g range: 16384 counts = 1 g
const float GYRO_PER_DPS  = 131.0;      // +/-250 deg/s range: 131 counts = 1 deg/s

void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);                // which register
  Wire.write(value);              // new value
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);                // point at the register...
  Wire.endTransmission(false);    // ...keep the bus (repeated start)...
  Wire.requestFrom(MPU_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;   // ...and read one byte back
}

// Try every I2C address and list the ones that answer.
void scanBus() {
  Serial.println("Scanning I2C0 (SDA GP4, SCL GP5)...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {          // 0 = something acknowledged
      Serial.print("  device at 0x");
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) Serial.println("  nothing answered: check VCC (pin 36), GND (pin 8), and SDA/SCL not swapped");
}

void printValue(const char *label, float v, int decimals) {
  Serial.print(label);
  if (v >= 0) Serial.print(' ');                // keep columns lined up
  Serial.print(v, decimals);
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000) {}

  Wire.setSDA(PIN_SDA);           // tell the Pico which pins are I2C0
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  Wire.setClock(400000);          // 400 kHz "fast mode"

  scanBus();

  uint8_t id = readReg(REG_WHO_AM_I);
  Serial.print("WHO_AM_I = 0x");
  Serial.print(id, HEX);
  Serial.println(id == 0x68 ? "  (MPU-6050 found)" : "  (expected 0x68: check wiring)");

  writeReg(REG_PWR_MGMT_1, 0x01);   // wake up, use the gyro's clock (more stable than the default)
  writeReg(REG_ACCEL_CONFIG, 0x00); // accelerometer +/-2 g
  writeReg(REG_GYRO_CONFIG, 0x00);  // gyro +/-250 deg/s
  delay(100);                       // give it a moment to start measuring

  Serial.println("Readings (hold the robot still, then tilt it slowly forward and back):");
}

void loop() {
  // Read 14 bytes in one go, starting at ACCEL_XOUT_H. Each value is 16 bits, high byte first.
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (uint8_t)14);
  if (Wire.available() < 14) {
    Serial.println("read failed: lost the MPU-6050");
    delay(500);
    return;
  }
  int16_t raw[7];
  for (int i = 0; i < 7; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);  // glue the two bytes into one signed number
  }

  // Convert raw counts into real units.
  float ax = raw[0] / ACCEL_PER_G;      // g (1 g = gravity)
  float ay = raw[1] / ACCEL_PER_G;
  float az = raw[2] / ACCEL_PER_G;
  float tempC = raw[3] / 340.0 + 36.53; // formula from the datasheet
  float gx = raw[4] / GYRO_PER_DPS;     // degrees per second
  float gy = raw[5] / GYRO_PER_DPS;
  float gz = raw[6] / GYRO_PER_DPS;

  // Tilt from gravity alone: only right while the robot is still (moving adds extra acceleration).
  float tiltX = atan2(ay, az) * 180.0 / PI;                      // rotation around the chip's X axis
  float tiltY = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI; // rotation around the chip's Y axis

  printValue("accel g  x", ax, 2);
  printValue("  y", ay, 2);
  printValue("  z", az, 2);
  printValue("  | gyro deg/s  x", gx, 1);
  printValue("  y", gy, 1);
  printValue("  z", gz, 1);
  printValue("  | tilt X", tiltX, 1);
  printValue("  tilt Y", tiltY, 1);
  printValue("  | temp C", tempC, 1);
  Serial.println();

  delay(100);   // 10 readings per second is plenty for looking at
}
