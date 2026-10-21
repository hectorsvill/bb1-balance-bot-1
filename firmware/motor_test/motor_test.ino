// motor_test.ino - Self-Balancing Robot, test 1: motors + encoders
//
// What it does: lets you spin each MG310 motor forward or backward through the
// D153C (TB6612) and counts the encoder pulses while it turns.
// Nothing moves until you type a command, and every run stops by itself.
//
// Board: Raspberry Pi Pico W, "Raspberry Pi Pico/RP2040" core by Earle Philhower.
// Serial monitor: 115200 baud, line ending "Newline".
//
// SAFETY: lift the robot so both wheels are off the table before you run a motor.
// The battery must be connected and the D153C switch on, or nothing will turn.

// ---------- pins (same as the Connections sheet) ----------
const int PIN_STBY = 9;    // HIGH = driver awake, LOW = both motors off
const int PIN_PWMA = 10;   // motor A speed (PWM)
const int PIN_PWMB = 11;   // motor B speed (PWM)
const int PIN_AIN1 = 12;   // motor A direction pair
const int PIN_AIN2 = 13;
const int PIN_BIN1 = 14;   // motor B direction pair
const int PIN_BIN2 = 15;
const int PIN_E1A  = 16;   // encoder 1 (motor A), channel A
const int PIN_E1B  = 17;   // encoder 1 (motor A), channel B
const int PIN_E2A  = 18;   // encoder 2 (motor B), channel A
const int PIN_E2B  = 19;   // encoder 2 (motor B), channel B

// ---------- test settings ----------
const int PWM_FREQ = 20000;            // 20 kHz: above hearing, so the motors don't whine
const int PWM_MAX  = 255;              // analogWrite range: 0 = off, 255 = full battery voltage
const unsigned long RUN_MS = 2000;     // each test run lasts 2 s
const long COUNTS_PER_REV = 520;       // our counting gives 2 x 13 pulses x 20:1 gearbox per wheel turn (check it with "e")
int testSpeed = 100;                   // about 40% power; change it with "speed N"

// ---------- directions found in test 1 (front = the "hectorsvill" side) ----------
// The motors face opposite ways on the chassis, so one of each pair is flipped in software.
// "f" and "r" use these so that forward means forward for both wheels.
const bool LEFT_MOTOR_REVERSED  = true;    // motor A (left):  "a-" rolls forward
const bool RIGHT_MOTOR_REVERSED = false;   // motor B (right): "b+" rolls forward
const bool LEFT_ENC_REVERSED    = false;   // encoder 1 already counts up rolling forward
const bool RIGHT_ENC_REVERSED   = true;    // encoder 2 counts down rolling forward

// ---------- encoder counters ----------
// "volatile" tells the compiler these change inside an interrupt,
// so it must read the real value every time instead of a cached copy.
volatile long countA = 0;
volatile long countB = 0;

// Interrupt routines: the Pico runs one of these the instant channel A changes
// (rising or falling edge). Channel B is a quarter-step behind A, so at that
// moment B tells us the direction: same level as A one way, different the other.
void isrEncoderA() {
  bool a = digitalRead(PIN_E1A);
  bool b = digitalRead(PIN_E1B);
  countA += (a == b) ? 1 : -1;
}

void isrEncoderB() {
  bool a = digitalRead(PIN_E2A);
  bool b = digitalRead(PIN_E2B);
  countB += (a == b) ? 1 : -1;
}

// Copy a counter with interrupts paused, so we never read it halfway through an update.
long readCount(volatile long &c) {
  noInterrupts();
  long v = c;
  interrupts();
  return v;
}

// TB6612 rules for each motor (IN1, IN2):
//   HIGH, LOW  -> spin one way        LOW, HIGH -> spin the other way
//   HIGH, HIGH -> brake (shorts the motor so it stops fast)
// The PWM pin sets how much of the battery voltage the motor gets.
void setMotor(char motor, int speed) {          // speed: -255..255, 0 = brake
  int in1 = (motor == 'A') ? PIN_AIN1 : PIN_BIN1;
  int in2 = (motor == 'A') ? PIN_AIN2 : PIN_BIN2;
  int pwm = (motor == 'A') ? PIN_PWMA : PIN_PWMB;
  if (speed > 0)      { digitalWrite(in1, HIGH); digitalWrite(in2, LOW);  }
  else if (speed < 0) { digitalWrite(in1, LOW);  digitalWrite(in2, HIGH); }
  else                { digitalWrite(in1, HIGH); digitalWrite(in2, HIGH); }
  analogWrite(pwm, min(abs(speed), PWM_MAX));
}

void stopAll() {
  setMotor('A', 0);
  setMotor('B', 0);
}

// Robot-level helpers: forward is positive for both wheels and both encoders.
void drive(int left, int right) {
  setMotor('A', LEFT_MOTOR_REVERSED  ? -left  : left);
  setMotor('B', RIGHT_MOTOR_REVERSED ? -right : right);
}
long leftCount()  { long c = readCount(countA); return LEFT_ENC_REVERSED  ? -c : c; }
long rightCount() { long c = readCount(countB); return RIGHT_ENC_REVERSED ? -c : c; }

// Throw away anything left in the serial buffer (for example the second half of a CR+LF).
void flushInput() {
  delay(50);
  while (Serial.available()) Serial.read();
}

void printHelp() {
  Serial.println();
  Serial.println("Motor test commands (type one, then Enter):");
  Serial.println("  a+   motor A forward for 2 s      a-   motor A backward for 2 s");
  Serial.println("  b+   motor B forward for 2 s      b-   motor B backward for 2 s");
  Serial.print  ("  speed N   set the test speed, 0-255 (now ");
  Serial.print(testSpeed);
  Serial.println(")");
  Serial.println("  f    both wheels forward for 2 s  r    both wheels backward for 2 s");
  Serial.println("  e    watch the wheels while you turn them by hand (Enter to stop)");
  Serial.println("Press Enter during a run to stop early.");
}

// Run one motor in one direction, then report how far its encoder moved.
void runTest(char motor, int direction) {
  volatile long &counter = (motor == 'A') ? countA : countB;
  long start = readCount(counter);
  flushInput();

  Serial.print("Motor ");
  Serial.print(motor);
  Serial.print(direction > 0 ? " forward (+)" : " backward (-)");
  Serial.print(" at ");
  Serial.print(testSpeed);
  Serial.println("/255 ...");

  setMotor(motor, direction * testSpeed);
  unsigned long t0 = millis();
  bool stoppedEarly = false;
  while (millis() - t0 < RUN_MS) {
    if (Serial.available()) {                   // any key stops the run
      while (Serial.available()) Serial.read();
      stoppedEarly = true;
      break;
    }
    delay(5);
  }
  unsigned long ranMs = millis() - t0;
  setMotor(motor, 0);
  delay(300);                                   // let the wheel come to rest

  long moved = readCount(counter) - start;
  Serial.print("  encoder ");
  Serial.print(motor == 'A' ? "1" : "2");
  Serial.print(" moved ");
  Serial.print(moved);
  Serial.print(" counts (about ");
  Serial.print((float)moved / COUNTS_PER_REV, 1);
  Serial.print(" wheel turns, ");
  Serial.print(moved * 1000L / (long)max(ranMs, 1UL));
  Serial.println(" counts/s)");
  if (stoppedEarly) Serial.println("  (stopped early)");
  if (abs(moved) < 10) Serial.println("  ! almost no counts: check that encoder's wires and the battery");
}

// Run both wheels the same way using the direction settings above.
// Both counts should come out positive for "f" and negative for "r".
void runDrive(int direction) {
  long l0 = leftCount(), r0 = rightCount();
  flushInput();
  Serial.print(direction > 0 ? "Both wheels forward" : "Both wheels backward");
  Serial.print(" at ");
  Serial.print(testSpeed);
  Serial.println("/255 ...");
  drive(direction * testSpeed, direction * testSpeed);
  unsigned long t0 = millis();
  while (millis() - t0 < RUN_MS && !Serial.available()) delay(5);
  drive(0, 0);
  flushInput();
  delay(300);
  long l = leftCount() - l0, r = rightCount() - r0;
  Serial.print("  left moved ");
  Serial.print(l);
  Serial.print(", right moved ");
  Serial.print(r);
  bool ok = (direction > 0) ? (l > 0 && r > 0) : (l < 0 && r < 0);
  Serial.println(ok ? "  (signs correct)" : "  ! signs wrong: check the four direction settings");
}

// Show how far each wheel has turned since you typed "e", in forward-positive counts.
// Prints only when something changes. Press Enter to stop.
void watchEncoders() {
  Serial.println("Turn each wheel one full turn FORWARD by hand. Press Enter when done.");
  flushInput();
  long l0 = leftCount(), r0 = rightCount();
  long lastL = 0, lastR = 0;
  while (!Serial.available()) {
    long l = leftCount() - l0, r = rightCount() - r0;
    if (l != lastL || r != lastR) {
      Serial.print("  left ");
      Serial.print(l);
      Serial.print(" (");
      Serial.print((float)l / COUNTS_PER_REV, 2);
      Serial.print(" turns)    right ");
      Serial.print(r);
      Serial.print(" (");
      Serial.print((float)r / COUNTS_PER_REV, 2);
      Serial.println(" turns)");
      lastL = l;
      lastR = r;
    }
    delay(100);
  }
  flushInput();
  Serial.print("Done. Left ");
  Serial.print(lastL);
  Serial.print(" counts, right ");
  Serial.print(lastR);
  Serial.println(" counts.");
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) return;
  Serial.print("> ");
  Serial.println(cmd);
  if      (cmd == "a+") runTest('A', +1);
  else if (cmd == "a-") runTest('A', -1);
  else if (cmd == "b+") runTest('B', +1);
  else if (cmd == "b-") runTest('B', -1);
  else if (cmd == "f")  runDrive(+1);
  else if (cmd == "r")  runDrive(-1);
  else if (cmd == "e")  watchEncoders();
  else if (cmd.startsWith("speed")) {
    testSpeed = constrain(cmd.substring(5).toInt(), 0, PWM_MAX);
    Serial.print("Test speed is now ");
    Serial.print(testSpeed);
    Serial.println("/255");
  }
  else printHelp();
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000) {}    // wait up to 5 s for the serial monitor

  // Outputs first, all off, with the driver asleep while we set up.
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);
  const int dirPins[] = {PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2};
  for (int p : dirPins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  analogWriteFreq(PWM_FREQ);
  analogWriteRange(PWM_MAX);
  analogWrite(PIN_PWMA, 0);
  analogWrite(PIN_PWMB, 0);

  // Encoder inputs, with an interrupt on each channel A.
  const int encPins[] = {PIN_E1A, PIN_E1B, PIN_E2A, PIN_E2B};
  for (int p : encPins) pinMode(p, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_E1A), isrEncoderA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_E2A), isrEncoderB, CHANGE);

  stopAll();
  digitalWrite(PIN_STBY, HIGH);                 // driver awake; motors still stopped
  Serial.println("Motor test ready. Wheels off the table?");
  printHelp();
}

// Collect typed characters until Enter, then run the command.
String line;
void loop() {
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
