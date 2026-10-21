// motors.h - the D153C (TB6612) motor driver and the two wheel encoders.
//
// Same wiring and rules as firmware/motor_test, tidied up for bb1. Only core 1
// calls these: it set up the pins and the encoder interrupts in setup1(), so the
// interrupts also run on core 1 and never disturb Wi-Fi on core 0.
//
// Everything here is "robot-level": forward is positive for both wheels and
// both encoders. The *_REVERSED settings in config.h hide which motor is
// mounted which way round.

#pragma once
#include "config.h"

// ---------- encoders ----------
//
// Each motor has a Hall encoder with two channels, A and B, a quarter step
// apart. We get an interrupt on every change of channel A (both edges). At that
// moment channel B tells the direction: same level as A one way, different the
// other. "volatile" because the interrupt changes them behind the loop's back.
volatile int32_t encCountLeft = 0;      // raw, before the *_ENC_REVERSED flip
volatile int32_t encCountRight = 0;

void isrEncoderLeft() {
  bool a = digitalRead(PIN_E1A);
  bool b = digitalRead(PIN_E1B);
  encCountLeft += (a == b) ? 1 : -1;
}

void isrEncoderRight() {
  bool a = digitalRead(PIN_E2A);
  bool b = digitalRead(PIN_E2B);
  encCountRight += (a == b) ? 1 : -1;
}

// Forward-positive counts since boot. A 32-bit read is a single instruction on
// the RP2040, so the interrupt can't change it halfway through.
int32_t encoderLeft()  { return LEFT_ENC_REVERSED  ? -encCountLeft  : encCountLeft; }
int32_t encoderRight() { return RIGHT_ENC_REVERSED ? -encCountRight : encCountRight; }

// ---------- motors ----------
//
// TB6612 rules for each motor (IN1, IN2):
//   HIGH, LOW  -> spin one way        LOW, HIGH -> spin the other way
//   HIGH, HIGH -> brake (shorts the motor so it stops fast)
// The PWM pin sets how much of the battery voltage the motor gets.
// STBY LOW puts the whole chip to sleep: both motors off, whatever else is set.

void setMotorRaw(int in1, int in2, int pwmPin, int speed) {   // speed -255..255, 0 = brake
  if (speed > 0)      { digitalWrite(in1, HIGH); digitalWrite(in2, LOW);  }
  else if (speed < 0) { digitalWrite(in1, LOW);  digitalWrite(in2, HIGH); }
  else                { digitalWrite(in1, HIGH); digitalWrite(in2, HIGH); }
  analogWrite(pwmPin, min(abs(speed), PWM_MAX));
}

// Robot-level: forward is positive for both wheels.
void motorsDrive(int left, int right) {
  setMotorRaw(PIN_AIN1, PIN_AIN2, PIN_PWMA, LEFT_MOTOR_REVERSED  ? -left  : left);
  setMotorRaw(PIN_BIN1, PIN_BIN2, PIN_PWMB, RIGHT_MOTOR_REVERSED ? -right : right);
}

void motorsEnable(bool on) {
  digitalWrite(PIN_STBY, on ? HIGH : LOW);
}

// Brake both wheels and put the driver to sleep.
void motorsOff() {
  motorsDrive(0, 0);
  motorsEnable(false);
}

void motorsBegin() {
  pinMode(PIN_STBY, OUTPUT);
  motorsEnable(false);                       // asleep while we set up
  const int outPins[] = {PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2};
  for (int p : outPins) pinMode(p, OUTPUT);
  analogWriteFreq(PWM_FREQ);
  analogWriteRange(PWM_MAX);
  motorsOff();

  const int encPins[] = {PIN_E1A, PIN_E1B, PIN_E2A, PIN_E2B};
  for (int p : encPins) pinMode(p, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_E1A), isrEncoderLeft, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_E2A), isrEncoderRight, CHANGE);
}

// ---------- wheel speed ----------
//
// Counts moved over the last SPEED_WINDOW ticks, scaled to counts per second.
// Call once per tick. Keeps a small ring of past counts, like the log buffer.
struct WheelSpeed {
  int32_t past[SPEED_WINDOW] = {};
  int     next = 0;
  bool    full = false;

  float update(int32_t count) {
    int32_t old = past[next];                // the count SPEED_WINDOW ticks ago
    bool valid = full;                       // false for the first SPEED_WINDOW ticks
    past[next] = count;
    next = (next + 1) % SPEED_WINDOW;
    if (next == 0) full = true;
    return valid ? (count - old) * (float)LOOP_HZ / SPEED_WINDOW : 0;
  }
};
