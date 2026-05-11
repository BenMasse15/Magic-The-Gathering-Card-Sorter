#include <ESP32Servo.h>

Servo fs5103r;
const int SERVO_PIN = 1;

// ── Forward declarations ─────────────────────────
void servoStop();
void servoForwardFull();
void servoBackwardFull();
void servoSetSpeed(int speed);
void servoForwardFor(int ms);
void servoBackwardFor(int ms);
void servoRunFor(int speed, int ms);

// ── Setup ────────────────────────────────────────
void setup() {
  ESP32PWM::allocateTimer(0);
  fs5103r.setPeriodHertz(50);
  fs5103r.attach(SERVO_PIN, 1000, 2000);
  servoStop();
}

// ── Control Functions ────────────────────────────
void servoStop()           { fs5103r.write(90); }
void servoForwardFull()    { fs5103r.write(180); }
void servoBackwardFull()   { fs5103r.write(0); }

void servoSetSpeed(int speed) {
  speed = constrain(speed, -100, 100);
  fs5103r.write(map(speed, -100, 100, 0, 180));
}

void servoForwardFor(int ms)        { servoForwardFull(); delay(ms); servoStop(); }
void servoBackwardFor(int ms)       { servoBackwardFull(); delay(ms); servoStop(); }
void servoRunFor(int speed, int ms) { servoSetSpeed(speed); delay(ms); servoStop(); }

// ── Loop ─────────────────────────────────────────
void loop() {
  servoForwardFor(2000);
  delay(500);
  servoBackwardFor(2000);
  delay(500);
  servoSetSpeed(50);
  delay(1500);
  servoStop();
  delay(500);
}