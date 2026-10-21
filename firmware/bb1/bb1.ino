// bb1.ino - BB1 balance bot main firmware.
//
// Step 1 of docs/FIRMWARE_PLAN.md: a steady pitch angle.
//   - measures the gyro bias at startup (keep the robot still for 2 s)
//   - runs a fixed 200 Hz loop: read IMU -> complementary filter -> pitch
//   - prints pitch, accel-only pitch, pitch rate and loop time 10 times a second
// Motors are held OFF (STBY low) the whole time. Nothing moves in this step.
//
// Board: Raspberry Pi Pico W, arduino-pico core by Earle Philhower (6.3.0).
// Serial: 115200 baud, line ending "Newline". Type "help" for commands.
// Power rule (no 1N5819 yet): D153C on before USB; USB out before D153C off.
//
// Files:
//   config.h  pins, signs, rates      imu.h    MPU-6050 driver + bias calibration
//   angle.h   complementary filter

#include "config.h"
#include "imu.h"
#include "angle.h"

Angle angle;

// Loop timing stats, reset every printout.
unsigned long loopWorkMaxUs = 0;   // longest time spent doing work in one tick
unsigned long loopWorkSumUs = 0;
unsigned long loopTicks = 0;
unsigned long imuErrors = 0;
unsigned long overruns = 0;        // ticks where the work took longer than 5 ms

bool csvMode = false;              // "csv": print comma-separated lines for a spreadsheet

void printHelp() {
  Serial.println();
  Serial.println("BB1 step 1 commands:");
  Serial.println("  cal       measure the gyro bias again (hold the robot still)");
  Serial.println("  alpha N   set the filter weight, 0.90 to 0.999 (now " + String(angle.alpha, 3) + ")");
  Serial.println("  csv       switch to comma-separated output (again to switch back)");
  Serial.println("  help      this list");
}

void runCalibration() {
  Serial.println("Calibrating gyro: keep the robot still for 2 s...");
  if (imuCalibrate()) {
    Serial.print("  bias deg/s  x ");
    Serial.print(gyroBias[0], 2);
    Serial.print("  y ");
    Serial.print(gyroBias[1], 2);
    Serial.print("  z ");
    Serial.println(gyroBias[2], 2);
  } else {
    Serial.println("  ! robot moved (or the IMU didn't answer): bias not changed. Type cal to retry.");
  }
  angle.started = false;   // restart the filter from the accelerometer angle
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;
  Serial.println("> " + cmd);
  if (cmd == "cal") {
    runCalibration();
  } else if (cmd.startsWith("alpha")) {
    float a = cmd.substring(5).toFloat();
    if (a >= 0.90 && a <= 0.999) {
      angle.alpha = a;
      Serial.println("alpha = " + String(a, 3));
    } else {
      Serial.println("alpha must be between 0.90 and 0.999");
    }
  } else if (cmd == "csv") {
    csvMode = !csvMode;
    if (csvMode) Serial.println("ms,pitch,accel_pitch,pitch_rate,loop_avg_us,loop_max_us");
  } else {
    printHelp();
  }
}

// Collect typed characters until Enter.
String line;
void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line = "";
    } else {
      line += c;
    }
  }
}

void printStatus() {
  unsigned long avg = loopTicks ? loopWorkSumUs / loopTicks : 0;
  if (csvMode) {
    Serial.print(millis());              Serial.print(',');
    Serial.print(angle.pitch, 2);        Serial.print(',');
    Serial.print(angle.accelPitch, 2);   Serial.print(',');
    Serial.print(angle.pitchRate, 2);    Serial.print(',');
    Serial.print(avg);                   Serial.print(',');
    Serial.println(loopWorkMaxUs);
  } else {
    char buf[140];
    snprintf(buf, sizeof(buf),
             "pitch %+6.1f | accel only %+6.1f | rate %+7.1f deg/s | loop avg %4lu us max %4lu us | overruns %lu imu errors %lu",
             angle.pitch, angle.accelPitch, angle.pitchRate, avg, loopWorkMaxUs, overruns, imuErrors);
    Serial.println(buf);
  }
  loopWorkMaxUs = loopWorkSumUs = loopTicks = 0;
}

void setup() {
  // Motors off before anything else: STBY low puts the D153C to sleep.
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);

  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000) {}   // wait up to 5 s for a serial monitor

  Serial.println();
  Serial.println("BB1 step 1: pitch angle. Motors are off.");
  if (!imuBegin()) {
    Serial.println("! MPU-6050 not found at 0x68: check VCC (pin 36), GND (pin 8), SDA GP4, SCL GP5.");
  } else {
    Serial.println("MPU-6050 found.");
  }
  delay(500);              // let the chip settle and your hands leave the robot
  runCalibration();
  printHelp();
}

void loop() {
  // A fixed-rate loop: each tick starts exactly 5 ms after the previous one.
  // We remember when the next tick is due and wait for it, instead of using
  // delay(5), which would add the time our own work took on top.
  static unsigned long nextTick = micros();
  static unsigned long lastTick = micros();
  static unsigned long lastPrint = 0;

  while ((long)(micros() - nextTick) < 0) {}   // wait for the tick (cast handles wrap-around)
  unsigned long now = micros();
  float dt = (now - lastTick) / 1e6;           // seconds since the last tick, about 0.005
  lastTick = now;
  nextTick += LOOP_US;

  // ---- the work of one tick ----
  ImuSample s;
  if (imuRead(s)) {
    angleUpdate(angle, s, dt);
  } else {
    imuErrors++;
  }
  // ------------------------------

  unsigned long work = micros() - now;
  loopWorkSumUs += work;
  loopWorkMaxUs = max(loopWorkMaxUs, work);
  loopTicks++;
  if (work > LOOP_US) {
    overruns++;
    nextTick = micros() + LOOP_US;             // fell behind: don't try to catch up in a burst
  }

  // Slow jobs, outside the timed work. In step 2 these move to the other core.
  readSerial();
  if (millis() - lastPrint >= 1000 / PRINT_HZ) {
    lastPrint = millis();
    printStatus();
  }
}
