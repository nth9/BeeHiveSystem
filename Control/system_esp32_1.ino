#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ps5Controller.h>
#include <Arduino.h>
#include <math.h>
#include <HardwareSerial.h>

bool isSweeping = false;
unsigned long lastStepTime = 0;
int currentStep = 0;
int totalSteps = 0;
int s1_start, s1_end, s2_start, s2_end;
int currentStepDelay = 20;

// ==========================
// LCD Config
// ==========================
LiquidCrystal_I2C lcd(0x27, 20, 4);
int hiveNo = 1;
int stationNo = 1;

// ==========================
// UART Config
// ==========================
HardwareSerial MySerial(1);  // use UART1

// ==========================
// Motor Pins & PWM Config
// ==========================
#define L_IN1 27
#define L_IN2 26
// #define L_IN1 27 // Duplicate removed
// #define L_IN2 26 // Duplicate removed
#define L_EN  14  // PWM

#define R_IN1 25
#define R_IN2 33
#define R_EN  32  // PWM

const int MOTOR_FREQ = 30000;
const int MOTOR_RESOLUTION = 8;
const int MAX_SPEED = 250;

const float LEFT_CALIBRATION = 1.05;
const float RIGHT_CALIBRATION = 1.00;

float currentLeft = 0;
float currentRight = 0;
const float MOTOR_RAMP_STEP = 0.08;

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

bool sweepToggle = false;

// ==========================
// Button state tracking
// ==========================
bool upPrev = false;
bool downPrev = false;
bool leftPrev = false;
bool rightPrev = false;
bool trianglePrev = false;
bool CirclePrev = false;

// ==========================
// Non-blocking UART receive buffer
// ==========================
char uartBuf[128];
uint8_t uartIdx = 0;

// ==========================
// Non-blocking servo sweep state
// ==========================
bool sweepActive = false;
unsigned long sweepLastMs = 0;
int sweepStep = 0;
int sweepStepsTotal = 0;
int sweepFrom1 = 0, sweepTo1 = 0;
int sweepFrom2 = 0, sweepTo2 = 0;
int sweepDelayMs = 20;

// ==========================
// Trigger
// ==========================
// ENSURE THIS MATCHES YOUR WIRING (Moved to 23 based on previous conversation)
#define PIN_TRIG_FINISH 23 
#define PIN_TRIG_ERROR  13 
#define PIN_TRIG_CELL   2  
#define PIN_TRIG_BEE    15 

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

float rampMotor(float current, float target, float step) {
  if (fabs(target - current) < step) return target;
  return current + step * ((target > current) ? 1 : -1);
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

void moveServosTogether(int ch1, int from1, int to1,
                        int ch2, int from2, int to2,
                        int stepDelay = 10) {
  int steps = max(abs(to1 - from1), abs(to2 - from2));
  if (steps == 0) return; 

  for (int i = 0; i <= steps; i++) {
    // Calculate progress as a percentage (0.0 to 1.0)
    float progress = (float)i / (float)steps;
    
    // Linear interpolation: Start + (Total Distance * Progress)
    int a1 = from1 + (int)((to1 - from1) * progress);
    int a2 = from2 + (int)((to2 - from2) * progress);
    
    setServo(ch1, a1);
    setServo(ch2, a2);
    delay(stepDelay); 
  }
}

// ==========================
// UART message handler
// ==========================
void handleUartMessage(const String &msgIn) {
  String msg = msgIn;
  msg.trim();
  if (msg.length() == 0) return;

  // --- DEBUGGING PRINT ---
  // This will show exactly what came in from the Pi
  Serial.print("DEBUG RX: [");
  Serial.print(msg);
  Serial.println("]");
  // ----------------------

  if (msg.equalsIgnoreCase("SWEEP_BEE")) {
    Serial.println("Action: SWEEP_BEE triggered");
    // toggle sweep non-blocking
    if (!sweepToggle) {
      moveServosTogether(SERVO1_CH, 0, 180, SERVO2_CH, 180, 0, 20);
      sweepToggle = true;
    } else {
      moveServosTogether(SERVO1_CH, 180, 0, SERVO2_CH, 0, 180, 20);
      sweepToggle = false;
    }
    // send single-line confirmation
    String out = "FINISH_BEE,HIVE=" + String(hiveNo) + ",NO=" + String(stationNo);
    MySerial.println(out);
    return;
  }

  if (msg.equalsIgnoreCase("SWEEP_CELL")) {
    Serial.println("Action: SWEEP_CELL triggered");
    // toggle sweep non-blocking
    if (!sweepToggle) {
      moveServosTogether(SERVO1_CH, 0, 180, SERVO2_CH, 180, 0, 20);
      sweepToggle = true;
    } else {
      moveServosTogether(SERVO1_CH, 180, 0, SERVO2_CH, 0, 180, 20);
      sweepToggle = false;
    }
    // send single-line confirmation
    String out = "FINISH_CELL,HIVE=" + String(hiveNo) + ",NO=" + String(stationNo);
    MySerial.println(out);
    return;
  }

  if (msg.equalsIgnoreCase("FINISH_CAP")) {
    Serial.println("Action: FINISH triggered (Firing Pin 23)");
    digitalWrite(PIN_TRIG_FINISH, HIGH);
    delay(1000);
    digitalWrite(PIN_TRIG_FINISH, LOW);
    setServo(SERVO1_CH, 90);
    setServo(SERVO2_CH, 90);

    return;
  }
  
  if (msg.equalsIgnoreCase("ERROR")) {
    Serial.println("Action: ERROR triggered");
    digitalWrite(PIN_TRIG_ERROR, HIGH);
    delay(1000);
    digitalWrite(PIN_TRIG_ERROR, LOW);
    return;
  }
}

// ==========================
// Setup
// ==========================
void setup() {
  // Start Debug Serial (USB to Computer)
  Serial.begin(115200);
  Serial.println("ESP32 Main Controller Started. Waiting for Pi5...");

  // --- FIX: Force triggers LOW immediately ---
  // We do this before setting pinMode to OUTPUT to prevent glitches
  digitalWrite(PIN_TRIG_FINISH, LOW);
  digitalWrite(PIN_TRIG_ERROR, LOW);
  digitalWrite(PIN_TRIG_CELL, LOW);
  digitalWrite(PIN_TRIG_BEE, LOW);

  pinMode(PIN_TRIG_FINISH, OUTPUT);
  pinMode(PIN_TRIG_ERROR, OUTPUT);
  pinMode(PIN_TRIG_CELL, OUTPUT);
  pinMode(PIN_TRIG_BEE, OUTPUT);
  
  // LCD init
  Wire.begin(21, 22);
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" Hive |  Frame  ");
  lcd.setCursor(5, 1);
  lcd.print(hiveNo);
  lcd.setCursor(14, 1);
  lcd.print(stationNo);

  // UART init (UART1: RX=16, TX=17) - Connection to Pi5
  MySerial.begin(9600, SERIAL_8N1, 16, 17);

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
  setServo(SERVO1_CH, 90);
  setServo(SERVO2_CH, 90);
  
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
  // keep loop responsive for Bluetooth stack
  if (!ps5.isConnected()) {
    // still process UART messages even if PS5 disconnected
    while (MySerial.available()) {
      char c = MySerial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        uartBuf[uartIdx] = '\0';
        handleUartMessage(String(uartBuf));
        uartIdx = 0;
      } else {
        if (uartIdx < sizeof(uartBuf) - 1) uartBuf[uartIdx++] = c;
      }
    }
    delay(10);
    return;
  }

  // --- LCD Control (button presses) ---
  bool upNow = ps5.Up();
  bool downNow = ps5.Down();
  bool leftNow = ps5.Left();
  bool rightNow = ps5.Right();

  if (upNow && !upPrev) {
    hiveNo++;
    if (hiveNo > 99) hiveNo = 1;
    lcd.setCursor(5, 1); lcd.print("   ");
    lcd.setCursor(5, 1); lcd.print(hiveNo);
  }
  if (downNow && !downPrev) {
    hiveNo--;
    if (hiveNo < 1) hiveNo = 99;
    lcd.setCursor(5, 1); lcd.print("   ");
    lcd.setCursor(5, 1); lcd.print(hiveNo);
  }
  if (rightNow && !rightPrev) {
    stationNo++;
    if (stationNo > 9) stationNo = 1;
    lcd.setCursor(14, 1); lcd.print("   ");
    lcd.setCursor(14, 1); lcd.print(stationNo);
  }
  if (leftNow && !leftPrev) {
    stationNo--;
    if (stationNo < 1) stationNo = 9;
    lcd.setCursor(14, 1); lcd.print("   ");
    lcd.setCursor(14, 1); lcd.print(stationNo);
  }

  upPrev = upNow; downPrev = downNow;
  leftPrev = leftNow; rightPrev = rightNow;
  // -----------------------

  // --- Motor Control ---
  float forward = -ps5.RStickX() / 128.0;
  float turn    = ps5.LStickY() / 128.0;
  if (fabs(forward) < 0.08) forward = 0;
  if (fabs(turn) < 0.08) turn = 0;

  float leftTarget  = forward + turn;
  float rightTarget = forward - turn;
  float maxVal = max(fabs(leftTarget), fabs(rightTarget));
  if (maxVal > 1.0) {
    leftTarget  /= maxVal;
    rightTarget /= maxVal;
  }
  leftTarget  = copysign(pow(fabs(leftTarget), 1.5), leftTarget);
  rightTarget = copysign(pow(fabs(rightTarget), 1.5), rightTarget);

  if (forward != 0) {
    currentLeft  = rampMotor(currentLeft,  leftTarget,  MOTOR_RAMP_STEP);
    currentRight = rampMotor(currentRight, rightTarget, MOTOR_RAMP_STEP);
  } else {
    currentLeft  = leftTarget;
    currentRight = rightTarget;
  }

  setMotor(L_IN1, L_IN2, 0, currentLeft * LEFT_CALIBRATION);
  setMotor(R_IN1, R_IN2, 1, currentRight * RIGHT_CALIBRATION);
  // -----------------------

  // --- Non-blocking UART receive handling ---
  while (MySerial.available()) {
    char c = MySerial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      uartBuf[uartIdx] = '\0';
      handleUartMessage(String(uartBuf));
      uartIdx = 0;
    } else {
      if (uartIdx < sizeof(uartBuf) - 1) uartBuf[uartIdx++] = c;
    }
  }
  // -----------------------

  // --- Send hive/station info via UART only once per Triangle press ---
  bool triangleNow = ps5.Triangle();
  if (triangleNow && !trianglePrev) {
    // send single-line message (fast)
    moveServosTogether(SERVO1_CH, 90, 0, SERVO2_CH, 90, 180, 10);
    delay(20000);
    digitalWrite(PIN_TRIG_BEE, HIGH);
    delay(2500);
    digitalWrite(PIN_TRIG_BEE, LOW);
    String out = "Capture Bee,HIVE=" + String(hiveNo) + ",NO=" + String(stationNo);
    MySerial.println(out);
    Serial.println("Sent Command: " + out); // Debug
  }
  trianglePrev = triangleNow;

  bool CircleNow = ps5.Circle();
  if (CircleNow && !CirclePrev) {
    moveServosTogether(SERVO1_CH, 90, 0, SERVO2_CH, 90, 180, 10);
    delay(20000);
    digitalWrite(PIN_TRIG_CELL, HIGH);
    delay(2500);
    digitalWrite(PIN_TRIG_CELL, LOW);
    // send single-line message (fast)
    String out = "Capture Cell,HIVE=" + String(hiveNo) + ",NO=" + String(stationNo);
    MySerial.println(out);
    Serial.println("Sent Command: " + out); // Debug
  }
  CirclePrev = CircleNow;
  
  delay(10); 
}