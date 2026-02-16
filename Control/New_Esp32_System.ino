#include <Arduino.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <ps5Controller.h>
#include <math.h>
#include <HardwareSerial.h>
#include <LiquidCrystal_I2C.h>

// ==========================
// PIN DEFINITIONS
// ==========================

// --- Motor 1 (Left) ---
int RPWM1_Output = 27; 
int LPWM1_Output = 26;
int EN_Pin1 = 14;      

// --- Motor 2 (Right) ---
int RPWM2_Output = 33; 
int LPWM2_Output = 25;
int EN_Pin2 = 32;    

// PWM
const int freq = 5000;
const int resolution = 8; 
const int channel1 = 2;
const int channel2 = 3;

// UART
char uartBuf[128];
HardwareSerial Pi5_Serial(2);

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 2);
int hiveNo = 1;
int FrameNo = 1;

// Button tracking
bool upPrev = false;
bool downPrev = false;
bool leftPrev = false;
bool rightPrev = false;
bool trianglePrev = false;
bool circlePrev = false;

// ==========================
// Servo Pins & PWM Config
// ==========================
#define SERVO1_PIN 18
#define SERVO2_PIN 19
#define SERVO1_CH  0
#define SERVO2_CH  1

const int SERVO_MIN_US = 500;
const int SERVO_MAX_US = 2400;
const int SERVO_FREQ   = 50;

bool sweepToggle = false;

bool sweepActive = false;
unsigned long sweepLastMs = 0;
int sweepStep = 0;
int sweepStepsTotal = 0;
int sweepFrom1 = 0, sweepTo1 = 0;
int sweepFrom2 = 0, sweepTo2 = 0;
int sweepDelayMs = 20;

// Triggers
#define TRIG_FINISH 23 
#define TRIG_ERROR  13 
#define TRIG_CELL   2  
#define TRIG_BEE    15 

// =================================================
// MOTOR SYSTEM  (YOUR LOGIC KEPT)
// =================================================
void MotorSystem() {
    int ly = ps5.LStickY();
    int rx = ps5.RStickX(); 

    if (abs(ly) < 15) ly = 0;
    if (abs(rx) < 15) rx = 0;

    int fullSpeed = map(ly, -128, 127, -255, 255);
    int turn = map(rx, -128, 127, -255, 255);

    int leftPWM = 0;
    int rightPWM = 0;

    // ==============================
    // FORWARD / BACKWARD (STRAIGHT)
    // ==============================
    if (abs(turn) < 10 && abs(fullSpeed) > 10) {
        leftPWM  = fullSpeed;
        rightPWM = fullSpeed;
    }

    // ==============================
    // TURN ONLY (IN PLACE)
    // ==============================
    else if (abs(fullSpeed) < 10 && abs(turn) > 10) {
        int base = abs(turn);

        // your style: one side slower
        int slow = base * 0.3;   // 30% speed instead of -150

        if (turn > 0) {
            // turn RIGHT
            leftPWM  = base;
            rightPWM = slow;
        } else {
            // turn LEFT
            leftPWM  = slow;
            rightPWM = base;
        }
    }

    // ==============================
    // MOVE + TURN (CURVE)
    // ==============================
    else if (abs(fullSpeed) > 10 && abs(turn) > 10) {

        int base = abs(fullSpeed);
        int slow = base * 0.5;   // softer curve

        if (turn > 0) {
            // curve RIGHT
            leftPWM  = base;
            rightPWM = slow;
        } else {
            // curve LEFT
            leftPWM  = slow;
            rightPWM = base;
        }

        // keep sign for forward/backward
        if (fullSpeed < 0) {
            leftPWM  = -leftPWM;
            rightPWM = -rightPWM;
        }
    }

    // ==============================
    // STOP
    // ==============================
    else {
        leftPWM = 0;
        rightPWM = 0;
    }

    // ==============================
    // APPLY DIRECTION PINS
    // ==============================
    if (leftPWM >= 0) {
        digitalWrite(RPWM1_Output, HIGH);
        digitalWrite(LPWM1_Output, LOW);
    } else {
        digitalWrite(RPWM1_Output, LOW);
        digitalWrite(LPWM1_Output, HIGH);
    }

    if (rightPWM >= 0) {
        digitalWrite(RPWM2_Output, HIGH);
        digitalWrite(LPWM2_Output, LOW);
    } else {
        digitalWrite(RPWM2_Output, LOW);
        digitalWrite(LPWM2_Output, HIGH);
    }

    ledcWrite(channel1, abs(leftPWM));
    ledcWrite(channel2, abs(rightPWM));
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

// =================================================
// HELPER FUNCTIONS
// =================================================
void handleUartMessage(String msg) {
    Serial.println(msg);
}

void sendMessageToPi(String msg) {
    Pi5_Serial.println(msg);
}

void updateValue(int &val, int delta, int minV, int maxV, int col, int row) {
  val += delta;
  if (val > maxV) val = minV;
  if (val < minV) val = maxV;
  
  lcd.setCursor(col, row);
  lcd.print("   "); 
  lcd.setCursor(col, row);
  lcd.print(val);
}

// =================================================
// SETUP
// =================================================
void setup() {
  Serial.begin(115200);
  Pi5_Serial.begin(9600, SERIAL_8N1, 16, 17);

  // ----- Motor ----- //
  pinMode(RPWM1_Output, OUTPUT);
  pinMode(LPWM1_Output, OUTPUT);
  pinMode(RPWM2_Output, OUTPUT);
  pinMode(LPWM2_Output, OUTPUT);

  ledcSetup(channel1, freq, resolution);
  ledcSetup(channel2, freq, resolution);
  ledcAttachPin(EN_Pin1, channel1);
  ledcAttachPin(EN_Pin2, channel2);

  ledcWrite(channel1, 0);
  ledcWrite(channel2, 0);

  // Servo PWM
  ledcSetup(SERVO1_CH, SERVO_FREQ, 16);
  ledcSetup(SERVO2_CH, SERVO_FREQ, 16);
  ledcAttachPin(SERVO1_PIN, SERVO1_CH);
  ledcAttachPin(SERVO2_PIN, SERVO2_CH);
  setServo(SERVO1_CH, 90);
  setServo(SERVO2_CH, 90);

  Wire.begin(21, 22);
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Initializing...");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(" Hive |  Frame  ");
  updateValue(hiveNo, 0, 1, 99, 5, 1);
  updateValue(FrameNo, 0, 1, 9, 14, 1);

  pinMode(TRIG_FINISH, OUTPUT);
  pinMode(TRIG_ERROR, OUTPUT);
  pinMode(TRIG_CELL, OUTPUT);
  pinMode(TRIG_BEE, OUTPUT);

  ps5.begin("10:18:49:7D:6D:F4");
}

// =================================================
// LOOP
// =================================================
void loop(){

    // =========================
    // FAST MOTOR UPDATE ALWAYS
    // =========================
    if (ps5.isConnected()) {
        MotorSystem();
    } else {
        ledcWrite(channel1, 0);
        ledcWrite(channel2, 0);
    }

    // =========================
    // NON-BLOCKING UART READ
    // =========================
    static String uartLine = "";
    while (Pi5_Serial.available()) {
        char c = Pi5_Serial.read();
        if (c == '\n') {
            handleUartMessage(uartLine);
            uartLine = "";
        } else {
            uartLine += c;
        }
    }

    // =========================
    // BUTTONS (NO BLOCKING)
    // =========================
    bool upNow = ps5.Up();
    bool downNow = ps5.Down();
    bool leftNow = ps5.Left();
    bool rightNow = ps5.Right();

    if (upNow && !upPrev)       updateValue(hiveNo, 1, 1, 99, 5, 1);
    if (downNow && !downPrev)   updateValue(hiveNo,-1, 1, 99, 5, 1);
    if (rightNow && !rightPrev) updateValue(FrameNo,1, 1,  9, 14,1);
    if (leftNow && !leftPrev)   updateValue(FrameNo,-1,1,  9, 14,1);

    upPrev = upNow;
    downPrev = downNow;
    leftPrev = leftNow;
    rightPrev = rightNow;

    // =========================
    // TRIGGERS
    // =========================
    bool triangleNow = ps5.Triangle();
    if (triangleNow && !trianglePrev) {
        moveServosTogether(SERVO1_CH, 90, 0, SERVO2_CH, 90, 180, 10);
        delay(20000);
        digitalWrite(TRIG_BEE, HIGH);
        delay(2500);
        digitalWrite(TRIG_BEE, LOW);
        sendMessageToPi("Capture Cell,HIVE=" + String(hiveNo) + ",NO=" + String(FrameNo));
    }
    trianglePrev = triangleNow;

    bool circleNow = ps5.Circle();
    if (circleNow && !circlePrev) {
        moveServosTogether(SERVO1_CH, 90, 0, SERVO2_CH, 90, 180, 10);
        delay(20000);
        digitalWrite(TRIG_CELL, HIGH);
        delay(2500);
        digitalWrite(TRIG_CELL, LOW);
        sendMessageToPi("Capture Bee,HIVE=" + String(hiveNo) + ",NO=" + String(FrameNo));
    }
    circlePrev = circleNow;

    // 🔥 REMOVE delay → makes it REALTIME
}
