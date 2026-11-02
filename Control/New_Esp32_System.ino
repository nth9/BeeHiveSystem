#include <Arduino.h>
#include <ps5Controller.h>
#include <math.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PicoMQTT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================
// CONFIGURATION & PINS
// ==========================
const char* WIFI_SSID = "Bee_Wifi";
const char* WIFI_PASS = "24654081";

// Motor Left
int RPWM1_Output = 27;
int LPWM1_Output = 26;
int EN_Pin1      = 14;

// Motor Right
int RPWM2_Output = 25;
int LPWM2_Output = 33;
int EN_Pin2      = 32;

// LEDC channels
const int CH_MOTOR_L = 0;
const int CH_MOTOR_R = 1;
const int CH_SERVO1  = 4;
const int CH_SERVO2  = 5;

const int freq       = 1500;
const int resolution = 8;

int hiveNo  = 1;
int FrameNo = 1;
Preferences prefs;

#define SERVO1_PIN 18
#define SERVO2_PIN 19
int S1_MIN_US = 500;
int S1_MAX_US = 2500;

int S2_MIN_US = 385;
int S2_MAX_US = 2400;
const int SERVO_FREQ = 50;

// Relay pins trigger output pins repurposed:
//   23 TRIG_FINISH → GREEN  relay
//   13 TRIG_ERROR  → YELLOW relay
//   15 TRIG_BEE    → BLUE   relay
//   5  Free        → RED    relay
const int RELAY_RED_PIN    = 5;
const int RELAY_GREEN_PIN  = 23;
const int RELAY_YELLOW_PIN = 13;
const int RELAY_BLUE_PIN   = 15;

// I2C LCD : SDA=21, SCL=22
LiquidCrystal_I2C lcd(0x27, 20, 4);

PicoMQTT::Server mqtt;

// Motor ramp state 
float curL = 0, curR = 0;

// Button debounce
bool upPrev = false, downPrev = false, leftPrev = false, rightPrev = false;
bool trianglePrev = false, circlePrev = false, crossPrev = false, squarePrev = false, r1Prev = false;

// Scan state machine
enum ScanState { IDLE, FRONT_SCANNING, BACK_SCANNING, SCAN_DONE_OK, SCAN_DONE_ERROR };
ScanState scanState = IDLE;
unsigned long stageStart = 0;
const unsigned long TIMEOUT_MS = 30000; // 30s per stage

String lastType  = "";
String lastHive  = "1";
String lastFrame = "1";

// Flags set by bee_monitor/status MQTT callback
volatile bool frontFinished = false;
volatile bool backFinished  = false;
volatile bool errorReceived = false;

unsigned long lastBlinkMs = 0;
bool blinkState = false;

// WiFi reconnect state
bool wifiWasDown = false;
unsigned long lastReconnectMs = 0;

// =================================================
// HELPERS
// =================================================
void lcdStatus(String line0, String line1 = "", String line2 = "", String line3 = "") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line0);
    if (line1.length()) {
        lcd.setCursor(0, 1);
        lcd.print(line1);
    }
    if (line2.length()) {
        lcd.setCursor(0, 2);
        lcd.print(line2);
    }
    if (line3.length()) {
        lcd.setCursor(0, 3);
        lcd.print(line3);
    }
}

void lcdIdle() {
    if (WiFi.status() == WL_CONNECTED) {
        lcdStatus("READY",
                  "HIVE: " + String(hiveNo) + " FRAME: " + String(FrameNo));
    } else {
        lcdStatus("READY",
                  "WiFi OFFLINE",
                  "HIVE: " + String(hiveNo) + " FRAME: " + String(FrameNo));
    }
}

void resetRelays() {
    digitalWrite(RELAY_RED_PIN,    LOW);
    digitalWrite(RELAY_GREEN_PIN,  LOW);
    digitalWrite(RELAY_YELLOW_PIN, LOW);
    digitalWrite(RELAY_BLUE_PIN,   LOW);
}

void mqttSubscribe() {
    // Pi publishes here after each capture batch:
    // "Pi 5_1: BeeFront Finish" / "Pi 5_1: BeeBack Finish" / "Pi 5_1: ... Error"
    mqtt.subscribe("bee_monitor/status", [](const char* topic, const char* payload) {
        String s = String(payload);
        Serial.println("Status: " + s);
        if (s.indexOf("Finish") >= 0) {
            if      (s.indexOf("Front") >= 0) frontFinished = true;
            else if (s.indexOf("Back")  >= 0) backFinished  = true;
        }
        if (s.indexOf("Error") >= 0 || s.indexOf("error") >= 0) errorReceived = true;
    });
}

// Start a scan: publish Front command and enter state machine
void startScan(const char* type) {
    lastType  = String(type);
    lastHive  = String(hiveNo);
    lastFrame = String(FrameNo);

    frontFinished = false;
    backFinished  = false;
    errorReceived = false;

    String msg = lastType + ", HIVE=" + lastHive + ", FRAME=" + lastFrame + ", Front";
    Serial.println("Publishing: " + msg);
    mqtt.publish("bee_monitor/cmd", msg.c_str());

    scanState   = FRONT_SCANNING;
    stageStart  = millis();
    lastBlinkMs = millis();

    resetRelays();
    lcdStatus("SCANNING FRONT", lastType,
              "HIVE:" + lastHive + " FRM:" + lastFrame, "Blue = scanning");
}

// Scan state machine
void runScanState() {
    if (scanState == IDLE) return;

    unsigned long now = millis();

    // Done states
    if (scanState == SCAN_DONE_OK || scanState == SCAN_DONE_ERROR) {
        if (now - stageStart > 5000) {
            resetRelays();
            digitalWrite(RELAY_GREEN_PIN, HIGH);
            blinkState = false;                  
            scanState = IDLE;
            lcdIdle();
        }
        return;
    }

    // Blink blue while scanning (FRONT or BACK only)
    if (now - lastBlinkMs > 1000) {
        lastBlinkMs = now;
        blinkState  = !blinkState;
        digitalWrite(RELAY_BLUE_PIN, blinkState ? HIGH : LOW);
    }

    if (errorReceived) {
        resetRelays();
        digitalWrite(RELAY_YELLOW_PIN, HIGH);
        lcdStatus("ERROR", lastType,
                  "HIVE:" + lastHive + " FRM:" + lastFrame, "Yellow light ON");
        stageStart = now;
        scanState  = SCAN_DONE_ERROR;
        return;
    }

    if (now - stageStart > TIMEOUT_MS) {
        resetRelays();
        digitalWrite(RELAY_YELLOW_PIN, HIGH);
        lcdStatus("TIMEOUT", lastType,
                  "HIVE:" + lastHive + " FRM:" + lastFrame, "No Pi response");
        Serial.println("Timeout waiting for Pi response");
        stageStart = now;
        scanState  = SCAN_DONE_ERROR;
        return;
    }

    if (scanState == FRONT_SCANNING && frontFinished) {
        frontFinished = false;
        moveServosTogether(0, 180, 180, 0, 15);
        stageStart = millis();
        String backCmd = lastType + ", HIVE=" + lastHive + ", FRAME=" + lastFrame + ", Back";
        mqtt.publish("bee_monitor/cmd", backCmd.c_str());
        Serial.println("Back: " + backCmd);
        scanState = BACK_SCANNING;
        lcdStatus("SCANNING BACK", lastType,
                  "HIVE:" + lastHive + " FRM:" + lastFrame, "Blue = scanning");
    }

    if (scanState == BACK_SCANNING && backFinished) {
        backFinished = false;
        moveServosTogether(180, 0, 0, 180, 15);
        resetRelays();
        digitalWrite(RELAY_GREEN_PIN, HIGH);
        lcdStatus("DONE - OK", lastType,
                  "HIVE:" + lastHive + " FRM:" + lastFrame, "Green light ON");
        stageStart = millis();
        scanState  = SCAN_DONE_OK;
    }
}

// =================================================
// MOTOR CONTROL — runs on Core 1
// =================================================
void MotorSystem() {
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    if (now - lastUpdate < 20) return;
    lastUpdate = now;

    int ly = ps5.LStickY();
    int rx = ps5.RStickX();
    const int deadzone = 15;

    const float ALPHA_ACCEL = 0.08f;
    const float ALPHA_DECEL = 0.08f;
    float targetLeft = 0, targetRight = 0, baseSpeed = 0;

    static int activeStick = 0;
    if (activeStick == 0 && abs(ly) > deadzone)       activeStick = 1;
    else if (activeStick == 0 && abs(rx) > deadzone)  activeStick = 2;
    else if (activeStick == 1 && abs(ly) <= deadzone) activeStick = 0;
    else if (activeStick == 2 && abs(rx) <= deadzone) activeStick = 0;

    if (activeStick == 1) rx = 0;
    if (activeStick == 2) ly = 0;

    if (abs(ly) > deadzone) {
        baseSpeed = (float)map(abs(ly), deadzone, 128, 0, 240);
        if (ly > 0) baseSpeed = -baseSpeed;
    }

    float steerFactor = 0;
    if (abs(rx) > deadzone) {
        steerFactor = (float)(abs(rx) - deadzone) / (float)(128 - deadzone);
        if (steerFactor > 1.0f) steerFactor = 1.0f;
    }

    float outerSpeed = 0;
    if (abs(baseSpeed) > 0.5f) {
        outerSpeed = baseSpeed;
    } else if (abs(rx) > deadzone) {
        outerSpeed = (float)map(abs(rx), deadzone, 128, 0, 180);
    }

    float innerSpeed = outerSpeed * max(1.0f - steerFactor, 0.5f);

    if (rx > deadzone) {
        targetLeft  = outerSpeed;
        targetRight = innerSpeed;
    } else if (rx < -deadzone) {
        targetRight = outerSpeed;
        targetLeft  = innerSpeed;
    } else {
        targetLeft  = baseSpeed;
        targetRight = baseSpeed;
    }

    {
        bool accel = abs(targetLeft) > abs(curL);
        bool crossZero = (curL > 5.0f && targetLeft < 0.0f)
                      || (curL < -5.0f && targetLeft > 0.0f);
        float alpha = (accel && !crossZero) ? ALPHA_ACCEL : ALPHA_DECEL;
        curL += alpha * (targetLeft - curL);
        if (abs(curL - targetLeft) < 1.5f) curL = targetLeft;
        if (abs(targetLeft) < 1.0f && abs(curL) < 8.0f) curL = 0.0f;
    }

    {
        bool accel = abs(targetRight) > abs(curR);
        bool crossZero = (curR > 5.0f && targetRight < 0.0f)
                      || (curR < -5.0f && targetRight > 0.0f);
        float alpha = (accel && !crossZero) ? ALPHA_ACCEL : ALPHA_DECEL;
        curR += alpha * (targetRight - curR);
        if (abs(curR - targetRight) < 1.5f) curR = targetRight;
        if (abs(targetRight) < 1.0f && abs(curR) < 8.0f) curR = 0.0f;
    }

    if (curL > 0.5f) {
        digitalWrite(RPWM1_Output, HIGH);
        digitalWrite(LPWM1_Output, LOW);
    } else if (curL < -0.5f) {
        digitalWrite(RPWM1_Output, LOW);
        digitalWrite(LPWM1_Output, HIGH);
    } else {
        digitalWrite(RPWM1_Output, LOW);
        digitalWrite(LPWM1_Output, LOW);
    }
    ledcWrite(CH_MOTOR_L, (int)abs(curL));

    if (curR > 0.5f) {
        digitalWrite(RPWM2_Output, HIGH);
        digitalWrite(LPWM2_Output, LOW);
    } else if (curR < -0.5f) {
        digitalWrite(RPWM2_Output, LOW);
        digitalWrite(LPWM2_Output, HIGH);
    } else {
        digitalWrite(RPWM2_Output, LOW);
        digitalWrite(LPWM2_Output, LOW);
    }
    ledcWrite(CH_MOTOR_R, (int)abs(curR));
}

// =================================================
// SERVO HELPERS — run on Core 1
// =================================================
int angleToDuty(int angle, int min_us, int max_us) {
    int us = map(angle, 0, 180, min_us, max_us);
    return (int)(((float)us / 20000.0f) * 65535.0f);
}

void setServo(int channel, int angle) {
    if (channel == CH_SERVO1) {
        ledcWrite(channel, angleToDuty(angle, S1_MIN_US, S1_MAX_US));
    } else if (channel == CH_SERVO2) {
        ledcWrite(channel, angleToDuty(angle, S2_MIN_US, S2_MAX_US));
    }
}

void moveServosTogether(int from1, int to1, int from2, int to2, int stepDelay) {
    int steps = max(abs(to1 - from1), abs(to2 - from2));
    if (steps == 0) return;

    int d1s = angleToDuty(from1, S1_MIN_US, S1_MAX_US);
    int d1e = angleToDuty(to1, S1_MIN_US, S1_MAX_US);
    int d2s = angleToDuty(from2, S2_MIN_US, S2_MAX_US);
    int d2e = angleToDuty(to2, S2_MIN_US, S2_MAX_US);

    for (int i = 0; i <= steps; i++) {
        float t     = (float)i / (float)steps;
        float eased = 0.5f * (1.0f - cosf(t * PI));
        ledcWrite(CH_SERVO1, d1s + (int)((d1e - d1s) * eased));
        ledcWrite(CH_SERVO2, d2s + (int)((d2e - d2s) * eased));
        mqtt.loop();
        delay(stepDelay);
    }
    prefs.putInt("s1_pos", to1);
    prefs.putInt("s2_pos", to2);
}

// =================================================
// APP TASK — pinned to Core 1
// PS5 BT stack runs on Core 0 automatically
// =================================================
void appTask(void* param) {
    for (;;) {
        // WiFi reconnect — retry every 5s if down
        if (WiFi.status() != WL_CONNECTED) {
            if (!wifiWasDown) {
                wifiWasDown = true;
                lcdStatus("WiFi LOST", "Reconnecting...");
            }
            if (millis() - lastReconnectMs > 5000) {
                lastReconnectMs = millis();
                WiFi.disconnect();
                WiFi.begin(WIFI_SSID, WIFI_PASS);
            }
        } else if (wifiWasDown) {
            wifiWasDown = false;
            mqtt.begin();
            mqttSubscribe();
            lcdIdle();
        }

        mqtt.loop();
        runScanState();

        if (ps5.isConnected()) {
            MotorSystem();

            bool u = ps5.Up(), d = ps5.Down(), l = ps5.Left(), r = ps5.Right();
            bool hiveFrameChanged = false;
            if (u && !upPrev)    { hiveNo++;  if (hiveNo  > 99) hiveNo  = 1;  hiveFrameChanged = true; }
            if (d && !downPrev)  { hiveNo--;  if (hiveNo  < 1)  hiveNo  = 99; hiveFrameChanged = true; }
            if (r && !rightPrev) { FrameNo++; if (FrameNo > 9)  FrameNo = 1;  hiveFrameChanged = true; }
            if (l && !leftPrev)  { FrameNo--; if (FrameNo < 1)  FrameNo = 9;  hiveFrameChanged = true; }
            upPrev = u; downPrev = d; leftPrev = l; rightPrev = r;
            if (hiveFrameChanged && scanState == IDLE) lcdIdle();

            bool mod = ps5.L2() || ps5.R2();

            // Only start a new scan when idle and WiFi is up
            if (mod && ps5.Triangle() && !trianglePrev
                    && scanState == IDLE && WiFi.status() == WL_CONNECTED) {
                startScan("Bee");
            }
            if (mod && ps5.Circle() && !circlePrev
                    && scanState == IDLE && WiFi.status() == WL_CONNECTED) {
                startScan("Cell");
            }
            if (ps5.L2() && ps5.R1() && !r1Prev
                    && scanState == IDLE && WiFi.status() == WL_CONNECTED) {
                startScan("Cb");
            }
            if (mod && ps5.Cross()  && !crossPrev)  moveServosTogether(90, 0,  90, 180, 20);
            if (mod && ps5.Square() && !squarePrev) moveServosTogether(0,  90, 180, 90, 20);

            trianglePrev = ps5.Triangle();
            circlePrev   = ps5.Circle();
            crossPrev    = ps5.Cross();
            squarePrev   = ps5.Square();
            r1Prev       = ps5.R1();

        } else {
            // Emergency stop
            curL *= 0.85f;
            curR *= 0.85f;
            if (abs(curL) < 1.0f) curL = 0;
            if (abs(curR) < 1.0f) curR = 0;
            ledcWrite(CH_MOTOR_L, (int)abs(curL));
            ledcWrite(CH_MOTOR_R, (int)abs(curR));
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// =================================================
// SETUP
// =================================================
void setup() {
    Serial.begin(115200);

    Wire.begin(21, 22);
    lcd.begin();
    lcd.backlight();
    lcdStatus("ESP32 Booting..");

    // Motor pins
    pinMode(RPWM1_Output, OUTPUT);
    pinMode(LPWM1_Output, OUTPUT);
    pinMode(RPWM2_Output, OUTPUT);
    pinMode(LPWM2_Output, OUTPUT);
    digitalWrite(RPWM1_Output, LOW);
    digitalWrite(LPWM1_Output, LOW);
    digitalWrite(RPWM2_Output, LOW);
    digitalWrite(LPWM2_Output, LOW);

    // Motor LEDC
    ledcSetup(CH_MOTOR_L, freq, resolution);
    ledcAttachPin(EN_Pin1, CH_MOTOR_L);
    ledcSetup(CH_MOTOR_R, freq, resolution);
    ledcAttachPin(EN_Pin2, CH_MOTOR_R);
    ledcWrite(CH_MOTOR_L, 0);
    ledcWrite(CH_MOTOR_R, 0);

    // Relay pins
    pinMode(RELAY_RED_PIN,    OUTPUT);
    pinMode(RELAY_GREEN_PIN,  OUTPUT);
    pinMode(RELAY_YELLOW_PIN, OUTPUT);
    pinMode(RELAY_BLUE_PIN,   OUTPUT);
    resetRelays();
    digitalWrite(RELAY_GREEN_PIN, HIGH); // Green ON by default

    // Servo LEDC
    ledcSetup(CH_SERVO1, SERVO_FREQ, 16);
    ledcAttachPin(SERVO1_PIN, CH_SERVO1);
    ledcSetup(CH_SERVO2, SERVO_FREQ, 16);
    ledcAttachPin(SERVO2_PIN, CH_SERVO2);
    prefs.begin("robot-data", false);

    // Snap to last known position then sweep to home
    int s1Start = prefs.getInt("s1_pos", 90);
    int s2Start = prefs.getInt("s2_pos", 90);
    setServo(CH_SERVO1, s1Start);
    setServo(CH_SERVO2, s2Start);
    delay(200);
    moveServosTogether(s1Start, 90, s2Start, 90, 20);

    // PS5 
    delay(2000);
    ps5.begin("10:18:49:7D:6D:F4");
    delay(1000);

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    lcdStatus("Connecting WiFi..", String(WIFI_SSID));
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 40) {
        delay(500);
        Serial.print(".");
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected: " + WiFi.localIP().toString());
        mqttSubscribe();
        mqtt.begin();
        lcdIdle();
    } else {
        Serial.println("\nWiFi failed — will retry in appTask");
        lcdIdle();
        wifiWasDown = true;
    }

    // Launch app task pinned to Core 1
    xTaskCreatePinnedToCore(
        appTask,    // task function
        "AppTask",  // name
        16384,      // stack size (increased for WiFi/MQTT overhead)
        NULL,       // parameter
        1,          // priority
        NULL,       // handle
        1           // Core 1
    );
}

void loop() {
    // all logic runs in appTask on Core 1
    vTaskDelete(NULL);
}
