#include <Arduino.h>
#include <ps5Controller.h>

// Motor Pins
#define L_IN1 27
#define L_IN2 26
#define L_EN  14
#define R_IN1 25
#define R_IN2 33
#define R_EN  32

// PWM Config
const int MOTOR_FREQ = 30000;
const int MOTOR_RESOLUTION = 8;
const int MAX_SPEED = 255;
const int DEADZONE = 0.1;

const int L_PWM_CH = 0;
const int R_PWM_CH = 1;

void setMotor(int in1, int in2, int pwm_ch, int speed, bool forward) {
  digitalWrite(in1, forward ? HIGH : LOW);
  digitalWrite(in2, forward ? LOW : HIGH);
  ledcWrite(pwm_ch, speed);
}

void stopMotors() {
  ledcWrite(L_PWM_CH, 0);
  ledcWrite(R_PWM_CH, 0);
}

void setup() {
  Serial.begin(115200);
  ps5.attachOnConnect([](){ Serial.println("PS5 Connected."); });
  ps5.attachOnDisconnect([](){ Serial.println("PS5 Disconnected!"); });
  ps5.begin("10:18:49:7D:6D:F4");

  pinMode(L_IN1, OUTPUT);
  pinMode(L_IN2, OUTPUT);
  pinMode(R_IN1, OUTPUT);
  pinMode(R_IN2, OUTPUT);

  ledcSetup(L_PWM_CH, MOTOR_FREQ, MOTOR_RESOLUTION);
  ledcSetup(R_PWM_CH, MOTOR_FREQ, MOTOR_RESOLUTION);
  ledcAttachPin(L_EN, L_PWM_CH);
  ledcAttachPin(R_EN, R_PWM_CH);

  stopMotors();
}

void loop() {
  if (ps5.isConnected()) {
    float lx = ps5.LStickX(); // -1.0 to +1.0
    float ly = -ps5.LStickY(); // invert Y for natural forward

    Serial.printf("LX: %.2f | LY: %.2f\n", lx, ly);

    if (abs(ly) < DEADZONE && abs(lx) < DEADZONE) {
      stopMotors();
      return;
    }

    int leftSpeed = MAX_SPEED * constrain(ly + lx, -1.0, 1.0);
    int rightSpeed = MAX_SPEED * constrain(ly - lx, -1.0, 1.0);

    bool leftForward = leftSpeed >= 0;
    bool rightForward = rightSpeed >= 0;

    setMotor(L_IN1, L_IN2, L_PWM_CH, abs(leftSpeed), leftForward);
    setMotor(R_IN1, R_IN2, R_PWM_CH, abs(rightSpeed), rightForward);
  }
}
