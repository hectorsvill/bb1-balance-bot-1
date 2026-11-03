// imu.h - talk to the MPU-6050 (GY-521) over I2C, no library.
//
// Same register approach as firmware/imu_test, with settings tuned for balancing:
//   - gyro range +/-500 deg/s (a falling robot can spin faster than 250)
//   - the chip's own low-pass filter at about 44 Hz (cuts motor vibration noise)
//   - new data 200 times a second, matching our control loop

#pragma once
#include <Wire.h>
#include "config.h"

const uint8_t MPU_ADDR = 0x68;           // AD0 left open

// Registers (numbers from the MPU-6050 register map)
const uint8_t REG_SMPLRT_DIV   = 0x19;   // sample rate divider
const uint8_t REG_CONFIG       = 0x1A;   // digital low-pass filter (DLPF)
const uint8_t REG_GYRO_CONFIG  = 0x1B;
const uint8_t REG_ACCEL_CONFIG = 0x1C;
const uint8_t REG_ACCEL_XOUT_H = 0x3B;   // first of 14 data bytes
const uint8_t REG_PWR_MGMT_1   = 0x6B;
const uint8_t REG_WHO_AM_I     = 0x75;

const float ACCEL_PER_G  = 16384.0;      // +/-2 g range
const float GYRO_PER_DPS = 65.5;         // +/-500 deg/s range (half of the 131 used in imu_test)

// One reading. We keep the raw counts as well as the converted values:
// the raw numbers go into the logs, so a future model (or a better filter)
// can start from exactly what the chip said.
struct ImuSample {
  int16_t raw[7];          // ax, ay, az, temp, gx, gy, gz as the chip sent them
  float ax, ay, az;        // g
  float gx, gy, gz;        // deg/s, bias already subtracted
};

// Gyro bias: what each gyro axis reads while perfectly still (deg/s).
// Every MEMS gyro has some; ours was about +2.5 deg/s on Y. We measure it at
// startup and subtract it, otherwise the angle would drift away steadily.
// Until a calibration succeeds it holds the typical values from config.h.
float gyroBias[3] = { GYRO_BIAS_TYPICAL[0], GYRO_BIAS_TYPICAL[1], GYRO_BIAS_TYPICAL[2] };

void imuWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t imuReadReg(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);           // repeated start: keep the bus
  Wire.requestFrom(MPU_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// What startup had to do to find the chip (in the log header and on serial),
// so we can see how often a restart catches the bus stuck.
int imuBusPulses = 0;   // SCL pulses it took to free SDA (0 = the bus was fine)
int imuTries     = 0;   // WHO_AM_I attempts (1 = found first time, 0 = never found)

// Free a stuck I2C bus. Why it gets stuck: a software restart (OTA, Wi-Fi
// save, rp2040.reboot) resets the Pico but not the MPU-6050, which stays
// powered. If the restart lands in the middle of a read, the chip is still
// sending a byte and holds SDA low, waiting for clock pulses that never come.
// The Pico then sees a busy bus and can't reach the chip; only a power cycle
// (the battery off) cleared it. The standard fix (I2C spec, "bus clear"):
// clock SCL by hand until the chip lets go of SDA (at most 9 pulses: 8 data
// bits + the ack), then send a STOP so it's back to idle.
void imuBusRecover() {
  pinMode(PIN_IMU_SDA, INPUT_PULLUP);
  pinMode(PIN_IMU_SCL, OUTPUT);
  digitalWrite(PIN_IMU_SCL, HIGH);
  delayMicroseconds(10);
  imuBusPulses = 0;
  while (digitalRead(PIN_IMU_SDA) == LOW && imuBusPulses < 9) {
    digitalWrite(PIN_IMU_SCL, LOW);  delayMicroseconds(10);   // 50 kHz: slow is fine here
    digitalWrite(PIN_IMU_SCL, HIGH); delayMicroseconds(10);
    imuBusPulses++;
  }
  // STOP: SDA goes low -> high while SCL is high.
  pinMode(PIN_IMU_SDA, OUTPUT);
  digitalWrite(PIN_IMU_SDA, LOW);  delayMicroseconds(10);
  digitalWrite(PIN_IMU_SDA, HIGH); delayMicroseconds(10);
  pinMode(PIN_IMU_SDA, INPUT);     // hand both pins back; Wire.begin() takes them over
  pinMode(PIN_IMU_SCL, INPUT);
}

// Set up I2C and the chip. Returns false if the chip doesn't answer.
bool imuBegin() {
  imuBusRecover();                       // first: make sure the bus is idle
  Wire.setSDA(PIN_IMU_SDA);
  Wire.setSCL(PIN_IMU_SCL);
  Wire.begin();
  Wire.setClock(400000);                 // 400 kHz: 14 bytes take about 0.4 ms

  // Ask "who are you?" up to 3 times. Between tries, reset the chip itself
  // (PWR_MGMT_1 bit 7: all its registers back to power-on values, as if the
  // battery had been pulled) and give it 100 ms to come back.
  imuTries = 0;
  bool found = false;
  for (int attempt = 1; attempt <= 3 && !found; attempt++) {
    found = imuReadReg(REG_WHO_AM_I) == 0x68;
    if (found) imuTries = attempt;
    else { imuWrite(REG_PWR_MGMT_1, 0x80); delay(100); }
  }
  if (!found) return false;

  imuWrite(REG_PWR_MGMT_1, 0x01);        // wake up, clock from the gyro
  delay(50);
  imuWrite(REG_CONFIG, 0x03);            // DLPF setting 3: ~44 Hz accel, ~42 Hz gyro
  imuWrite(REG_SMPLRT_DIV, 4);           // 1 kHz / (1 + 4) = 200 Hz output
  imuWrite(REG_GYRO_CONFIG, 0x08);       // +/-500 deg/s
  imuWrite(REG_ACCEL_CONFIG, 0x00);      // +/-2 g
  delay(50);
  return true;
}

// Read all 14 data bytes in one go. Returns false if the read failed.
bool imuRead(ImuSample &s) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)14) != 14) return false;
  for (int i = 0; i < 7; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    s.raw[i] = (int16_t)((hi << 8) | lo);  // high byte first
  }
  s.ax = s.raw[0] / ACCEL_PER_G;
  s.ay = s.raw[1] / ACCEL_PER_G;
  s.az = s.raw[2] / ACCEL_PER_G;
  s.gx = s.raw[4] / GYRO_PER_DPS - gyroBias[0];
  s.gy = s.raw[5] / GYRO_PER_DPS - gyroBias[1];
  s.gz = s.raw[6] / GYRO_PER_DPS - gyroBias[2];
  return true;
}

// Measure the gyro bias by averaging CAL_SAMPLES readings while the robot is still.
// Returns false (and keeps the old bias) if the robot seemed to move.
bool imuCalibrate() {
  float sum[3] = {0, 0, 0};
  float lo = 1e9, hi = -1e9;             // spread of gyro X, to catch movement
  float oldBias[3] = {gyroBias[0], gyroBias[1], gyroBias[2]};
  gyroBias[0] = gyroBias[1] = gyroBias[2] = 0;   // read the gyro uncorrected

  ImuSample s;
  int good = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    if (imuRead(s)) {
      sum[0] += s.gx;
      sum[1] += s.gy;
      sum[2] += s.gz;
      lo = min(lo, s.gx);
      hi = max(hi, s.gx);
      good++;
    }
    delayMicroseconds(LOOP_US);
  }

  if (good < CAL_SAMPLES / 2 || hi - lo > CAL_MAX_SPREAD) {
    for (int i = 0; i < 3; i++) gyroBias[i] = oldBias[i];
    return false;
  }
  for (int i = 0; i < 3; i++) gyroBias[i] = sum[i] / good;
  return true;
}
