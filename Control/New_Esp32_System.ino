#include <VarSpeedServo.h>
#include <ps5Controller.h>
#include <Wire.h>
#include <math.h>
#include <HardwareSerial.h>
#include <FablabL298Driver.h>
#include <LiquidCrystal_I2C.h>

// ==========================
// PIN DEFINITIONS & GLOBALS
// ==========================

// UART Buffer
char uartBuf[128]; 
int uartIdx = 0;

// Define the Serial for Pi5 (Using UART2 on ESP32 is common)
// RX = 16, TX = 17 (Adjust these pins to your wiring!)
HardwareSerial Pi5_Serial(2); 

// Motors
const uint8_t L_EN = 14;
const uint8_t L_IN1 = 27;
const uint8_t L_IN2 = 26;

const uint8_t R_EN = 25;
const uint8_t R_IN1 = 33;
const uint8_t R_IN2 = 32;

FablabL298Driver motor_L(L_EN, L_IN1, L_IN2);
FablabL298Driver motor_R(R_EN, R_IN1, R_IN2);

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 2);
int hiveNo = 1;
int FrameNo = 1;

// Button State Tracking
bool upPrev = false;
bool downPrev = false;
bool leftPrev = false;
bool rightPrev = false;
bool trianglePrev = false;
bool CirclePrev = false;

// Servos
VarSpeedServo ArmSer1; // left
VarSpeedServo ArmSer2; // right

// Triggers
#define TRIG_FINISH 23 
#define TRIG_ERROR  13 
#define TRIG_CELL   2  
#define TRIG_BEE    15 

// ==========================
// HELPER FUNCTIONS
// ==========================

// 1. Placeholder for your UART logic
void handleUartMessage(String msg) {
    // Add your Pi5 command logic here
    // Example: if (msg == "SCAN") { ... }
    Serial.print("Received from Pi: ");
    Serial.println(msg);
}

void sendMessageToPi(String msg) {
    if (CircleNow && !CirclePrev){
        Pi5_Serial.println(msg);      // Send over UART
        Serial.print("Sent to Pi: "); // Debu
        Serial.println(msg);
    }
    if (triangleNow && !trianglePrev){
        Pi5_Serial.println(msg);      
        Serial.print("Sent to Pi: "); 
        Serial.println(msg);
    }
}

// 2. Helper to update LCD numbers
void updateValue(int &val, int delta, int minV, int maxV, int col, int row) {
  val += delta;
  if (val > maxV) val = minV;
  if (val < minV) val = maxV;
  
  lcd.setCursor(col, row);
  lcd.print("   "); // Clear old digits
  lcd.setCursor(col, row);
  lcd.print(val);
}

void MotorSystem() {
    int ly = ps5.LStickY(); // Forward/Backward
    int rx = ps5.RStickX(); // Turn
    
    // Deadzone
    if (abs(ly) < 10) ly = 0;
    if (abs(rx) < 10) rx = 0;

    // Map to Percentage (-100 to 100)
    // Assuming: ly > 0 is Forward, rx > 0 is Right
    int throttle = map(ly, -128, 127, 0, 100);
    int turn = map(rx, -128, 127, 0, 100);

    // Reduce turn sensitivity
    turn = turn * 0.6; 

    // Calculate Speeds
    int leftSpeed = throttle + turn;
    int rightSpeed = throttle - turn;

    // Constrain to valid PWM range (0 to 100)
    leftSpeed = constrain(leftSpeed, 0, 100);
    rightSpeed = constrain(rightSpeed, 0, 100);

    // Apply to Motor
    if (leftSpeed > 0) {
        motor_L.forward();
        motor_L.goPercentage(leftSpeed);
    } else if (leftSpeed < 0) {
        motor_L.backward();
        motor_L.goPercentage(abs(leftSpeed));
    } else {
        motor_L.stop();
    }
    if (rightSpeed > 0) {
        motor_R.forward();
        motor_R.goPercentage(rightSpeed);
    } else if (rightSpeed < 0) {
        motor_R.backward();
        motor_R.goPercentage(abs(rightSpeed));
    } else {
        motor_R.stop();
    }
}

// ==========================
// SETUP
// ==========================
void setup() {
  Serial.begin(115200); // Debug Serial
  
  // Initialize Pi5 Serial (RX: 16, TX: 17)
  Pi5_Serial.begin(9600, SERIAL_8N1, 16, 17); 

  // Servo
  ArmSer1.attach(18);
  ArmSer2.attach(19);
  ArmSer1.write(90, 15); 
  ArmSer2.write(90, 15); 

  // LCD
  Wire.begin(21, 22);
  lcd.init(); 
  lcd.backlight(); 
  lcd.setCursor(0,0);
  lcd.print("Initializing...");
  delay(1000); // Replaced lcd_wait
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Hive |  Frame  ");
  
  // Print initial values
  updateValue(hiveNo, 0, 1, 99, 5, 1);
  updateValue(FrameNo, 0, 1, 9, 14, 1);

  // Motor
  motor_L.begin();
  motor_R.begin();

  // Pins
  pinMode(TRIG_FINISH, OUTPUT);
  pinMode(TRIG_ERROR, OUTPUT);
  pinMode(TRIG_CELL, OUTPUT);
  pinMode(TRIG_BEE, OUTPUT);

  // PS5
  ps5.attachOnConnect([](){ Serial.println("PS5 Connected."); });
  ps5.attachOnDisconnect([](){ Serial.println("PS5 Disconnected!"); });
  ps5.begin("10:18:49:7D:6D:F4"); // Make sure this MAC is correct
  Serial.println("Waiting for PS5 Controller...");
}

// ==========================
// LOOP
// ==========================
void loop(){
   if (ps5.isConnected()){
    // ---------------------- //
    // Uart
    // ---------------------- //
        while (Pi5_Serial.available()) {
                int len = Pi5_Serial.readBytesUntil('\n', uartBuf, sizeof(uartBuf) - 1);
                if (len > 0) {
                    uartBuf[len] = '\0';
                    String msg = String(uartBuf);
                    msg.trim();
                    handleUartMessage(msg);
                }
        }
    }
      
    // ---------------------- //
    // Lcd
    // ---------------------- //
    bool up = ps5.Up();
    bool down = ps5.Down();
    bool left = ps5.Left();
    bool right = ps5.Right();

    if (up    && !upPrev)    updateValue(hiveNo,   1, 1, 99, 5, 1);
    if (down  && !downPrev)  updateValue(hiveNo,  -1, 1, 99, 5, 1);
    if (right && !rightPrev) updateValue(FrameNo,  1, 1,  9, 14, 1);
    if (left  && !leftPrev)  updateValue(FrameNo, -1, 1,  9, 14, 1);

    upPrev = up; 
    downPrev = down; 
    leftPrev = left; 
    rightPrev = right;

    // ---------------------- //
    // Motor
    // ---------------------- //
    MotorSystem();

    // ---------------------- //
    // Servo & Sending
    // ---------------------- //
    bool triangleNow = ps5.Triangle();
    if (triangleNow && !trianglePrev) {
        ArmSer1.write(0, 15); 
        ArmSer2.write(180, 15); 
        digitalWrite(PIN_TRIG_BEE, HIGH);
        delay(2500);
        digitalWrite(PIN_TRIG_BEE, LOW);
        sendMessageToPi("Capture Cell,HIVE=" + String(hiveNo) + ",NO=" + String(FrameNo))
    }
    trianglePrev = triangleNow;

    bool CircleNow = ps5.Circle();
    if (CircleNow && !CirclePrev) {
        ArmSer1.write(0, 15); 
        ArmSer2.write(180, 15);
        digitalWrite(PIN_TRIG_CELL, HIGH);
        delay(2500);
        digitalWrite(PIN_TRIG_CELL, LOW);
        sendMessageToPi("Capture Bee,HIVE=" + String(hiveNo) + ",NO=" + String(FrameNo))
    }
    CirclePrev = CircleNow;
    
    delay(10); 


}
