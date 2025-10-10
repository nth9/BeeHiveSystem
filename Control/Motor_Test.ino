#include <Arduino.h>
#include <ps5Controller.h>

// ==========================
// Motor Pins & PWM Config
// ==========================
#define L_IN1 27
#define L_IN2 26
#define L_EN  14  // PWM channel
#define R_IN1 25
#define R_IN2 33
#define R_EN  32  // PWM channel

const int MOTOR_FREQ = 30000;
const int MOTOR_RESOLUTION = 8;   // 8-bit PWM (0–255)
const int MAX_SPEED = 255;        // full speed

// ==========================
// PWM Channels
// ==========================
const int L_PWM_CH = 0;
const int R_PWM_CH = 1;

// ==========================
// Motor Control Functions
// ==========================
void forward() {
  digitalWrite(L_IN1, HIGH);
  digitalWrite(L_IN2, LOW);
  digitalWrite(R_IN1, HIGH);
  digitalWrite(R_IN2, LOW);
  ledcWrite(L_PWM_CH, MAX_SPEED);
  ledcWrite(R_PWM_CH, MAX_SPEED);
}

void backward() {
  digitalWrite(L_IN1, LOW);
  digitalWrite(L_IN2, HIGH);
  digitalWrite(R_IN1, LOW);
  digitalWrite(R_IN2, HIGH);
  ledcWrite(L_PWM_CH, MAX_SPEED);
  ledcWrite(R_PWM_CH, MAX_SPEED);
}

void turnCW() {
  digitalWrite(L_IN1, HIGH);
  digitalWrite(L_IN2, LOW);
  digitalWrite(R_IN1, LOW);
  digitalWrite(R_IN2, HIGH);
  ledcWrite(L_PWM_CH, MAX_SPEED);
  ledcWrite(R_PWM_CH, MAX_SPEED);
}

void turnCCW() {
  digitalWrite(L_IN1, LOW);
  digitalWrite(L_IN2, HIGH);
  digitalWrite(R_IN1, HIGH);
  digitalWrite(R_IN2, LOW);
  ledcWrite(L_PWM_CH, MAX_SPEED);
  ledcWrite(R_PWM_CH, MAX_SPEED);
}

void stopMotors() {
  ledcWrite(L_PWM_CH, 0);
  ledcWrite(R_PWM_CH, 0);
}

// ==========================
// Setup
// ==========================
void setup() {
  Serial.begin(115200);
  ps5.attachOnConnect([](){ Serial.println("PS5 Connected."); });
  ps5.attachOnDisconnect([](){ Serial.println("PS5 Disconnected!"); });
  ps5.begin("10:18:49:7D:6D:F4"); 
  Serial.println("Waiting for PS5 Controller..."); // your ESP32 BT MAC

  pinMode(L_IN1, OUTPUT);
  pinMode(L_IN2, OUTPUT);
  pinMode(R_IN1, OUTPUT);
  pinMode(R_IN2, OUTPUT);

  // Configure PWM
  ledcSetup(L_PWM_CH, MOTOR_FREQ, MOTOR_RESOLUTION);
  ledcSetup(R_PWM_CH, MOTOR_FREQ, MOTOR_RESOLUTION);
  ledcAttachPin(L_EN, L_PWM_CH);
  ledcAttachPin(R_EN, R_PWM_CH);

  stopMotors();
}

// ==========================
// Main Loop
// ==========================
void loop() {
  if (ps5.isConnected()) {
    float lx = ps5.LStickX(); // range -1.0 to +1.0
    float ly = ps5.LStickY(); // range -1.0 to +1.0

    Serial.printf("LX: %.2f | LY: %.2f\n", lx, ly);

    if (ly > 0.0f) forward();
    else if (ly < 0.0f) backward();
    else if (lx > 0.0f) turnCW();
    else if (lx < 0.0f) turnCCW();
    else stopMotors();
  }
}
