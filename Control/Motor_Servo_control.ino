#include <Arduino.h>
#include <ps5Controller.h>
#include <math.h>

// ==========================
// Motor Pins & PWM Config
// ==========================
#define L_IN1 27
#define L_IN2 26
#define L_EN  14  // PWM

#define R_IN1 25
#define R_IN2 33
#define R_EN  32  // PWM

const int MOTOR_FREQ = 30000;
const int MOTOR_RESOLUTION = 8;   // 8-bit PWM
const int MAX_SPEED = 250;        // Safer than full 255

const float LEFT_CALIBRATION = 1.05;  
const float RIGHT_CALIBRATION = 1.00;

float currentLeft = 0;
float currentRight = 0;
const float MOTOR_RAMP_STEP = 0.08;  // acceleration speed

// ==========================
// Servo Pins & PWM Config
// ==========================
#define SERVO1_PIN 18
#define SERVO2_PIN 19
#define SERVO1_CH  2
#define SERVO2_CH  3

const int SERVO_MIN_US = 500;   
const int SERVO_MAX_US = 2400;  
const int SERVO_FREQ   = 50;    

// --- State tracking ---
bool xPressed = false;
bool sweepToggle = false;  

// ==========================
// Motor Functions
// ==========================
void setupMotor(int enPin, int channel) {
  pinMode(enPin, OUTPUT);
  ledcSetup(channel, MOTOR_FREQ, MOTOR_RESOLUTION);
  ledcAttachPin(enPin, channel);
}

void setMotor(int in1, int in2, int pwmChannel, float speed) {
  speed = constrain(speed, -1.0, 1.0);

  if (speed > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else if (speed < 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }

  int pwm = abs(speed) * MAX_SPEED;
  ledcWrite(pwmChannel, pwm);
}

// ==========================
// Servo Functions
// ==========================
int angleToDuty(int angle) {
  int us = map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  int duty = (int)(((float)us / 20000.0f) * 65535.0f); 
  return duty;
}

void setServo(int channel, int angle) {
  ledcWrite(channel, angleToDuty(angle));
}

// Smooth sweep for 2 servos at the same time
void moveServosTogether(int ch1, int from1, int to1,
                        int ch2, int from2, int to2,
                        int stepDelay = 15) {
  int steps = max(abs(to1 - from1), abs(to2 - from2)); // biggest distance decides steps
  for (int i = 0; i <= steps; i++) {
    int a1 = from1 + (to1 - from1) * i / steps;
    int a2 = from2 + (to2 - from2) * i / steps;
    setServo(ch1, a1);
    setServo(ch2, a2);
    delay(stepDelay);
  }
}

float rampMotor(float current, float target, float step) {
  if (fabs(target - current) < step) return target;
  return current + step * ((target > current) ? 1 : -1);
}

// ==========================
// Setup
// ==========================
void setup() {
  Serial.begin(115200);

  // Motor pins
  pinMode(L_IN1, OUTPUT); pinMode(L_IN2, OUTPUT);
  pinMode(R_IN1, OUTPUT); pinMode(R_IN2, OUTPUT);
  setupMotor(L_EN, 0);
  setupMotor(R_EN, 1);

  // Servo PWM
  ledcSetup(SERVO1_CH, SERVO_FREQ, 16);
  ledcSetup(SERVO2_CH, SERVO_FREQ, 16);
  ledcAttachPin(SERVO1_PIN, SERVO1_CH);
  ledcAttachPin(SERVO2_PIN, SERVO2_CH);

  // Initial positions (smooth)
  moveServosTogether(SERVO1_CH, 0, 45, SERVO2_CH, 0, 135, 20);

  // PS5 Controller
  ps5.attachOnConnect([](){ Serial.println("PS5 Connected."); });
  ps5.attachOnDisconnect([](){ Serial.println("PS5 Disconnected!"); });
  ps5.begin("10:18:49:7D:6D:F4"); 
  Serial.println("Waiting for PS5 Controller...");
}

// ==========================
// Loop
// ==========================
void loop() {
  if (!ps5.isConnected()) return;

  // --- Read joystick input ---
  float forward = -ps5.RStickX() / 128.0;
  float turn    = ps5.LStickY() / 128.0;

  if (fabs(forward) < 0.08) forward = 0;
  if (fabs(turn) < 0.08) turn = 0;

  // --- Differential drive targets ---
  float leftTarget  = forward + turn;
  float rightTarget = forward - turn;

  // Normalize targets
  float maxVal = max(fabs(leftTarget), fabs(rightTarget));
  if (maxVal > 1.0) {
    leftTarget  /= maxVal;
    rightTarget /= maxVal;
  }

  // Apply curve response
  leftTarget  = copysign(pow(fabs(leftTarget), 1.5), leftTarget);
  rightTarget = copysign(pow(fabs(rightTarget), 1.5), rightTarget);

  // --- Ramp motor speeds ---
  if (forward != 0) {
    currentLeft  = rampMotor(currentLeft,  leftTarget,  MOTOR_RAMP_STEP);
    currentRight = rampMotor(currentRight, rightTarget, MOTOR_RAMP_STEP);
  } else {
    currentLeft  = leftTarget;
    currentRight = rightTarget;
  }

  // --- Send PWM to motors with calibration ---
  setMotor(L_IN1, L_IN2, 0, currentLeft * LEFT_CALIBRATION);
  setMotor(R_IN1, R_IN2, 1, currentRight * RIGHT_CALIBRATION);

  // --- Servo control ---
  if (ps5.Cross()) {
    if (!xPressed) {
      xPressed = true;
      moveServosTogether(SERVO1_CH, 45, 0, SERVO2_CH, 135, 180, 20);
      Serial.println("X pressed -> Move to 0° / 180°");
    }
  }

  if (ps5.Circle() && xPressed) {
    if (!sweepToggle) {
      moveServosTogether(SERVO1_CH, 0, 180, SERVO2_CH, 180, 0, 20);
      Serial.println("O pressed -> 180° position");
      sweepToggle = true;
    } else {
      moveServosTogether(SERVO1_CH, 180, 0, SERVO2_CH, 0, 180, 20);
      Serial.println("O pressed -> 0° position");
      sweepToggle = false;
    }
  }

  delay(10); // small loop delay
}
