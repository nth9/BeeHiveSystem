#include <VarSpeedServo.h>
#include <ps5Controller.h>
#include <Wire.h>
#include <math.h>
#include <HardwareSerial.h>
#include <FablabL298Driver.h>
#include <LiquidCrystal_I2C.h>

// ==========================
// Motors
// ==========================

// Motor pins
const uint8_t L_EN = 14;
const uint8_t L_IN1 = 27;
const uint8_t L_IN2 = 26;

const uint8_t R_EN = 25;
const uint8_t R_IN1 = 33;
const uint8_t R_IN2 = 32;

// Create a new instance of the Motor class
FablabL298Driver motor_L(L_EN, L_IN1, L_IN2);
FablabL298Driver motor_R(R_EN, R_IN1, R_IN2);

// ==========================
// LCD
// ==========================
LiquidCrystal_I2C lcd(0x27, 20, 2);
int hiveNo = 1;
int FrameNo = 1;

// ==========================
// Servo Pins
// ==========================
VarSpeedServo ArmSer1; // left servo
VarSpeedServo ArmSer2; // right servo

// ==========================
// Trigger
// ==========================
#define TRIG_FINISH 23 
#define TRIG_ERROR  13 
#define TRIG_CELL   2  
#define TRIG_BEE    15 

void setup() {
  Serial.begin(115200);

// Servo //
// -------------------------- //
  ArmSer1.attach(18);
  ArmSer2.attach(19);
  // move to 90° at speed 15
  ArmSer1.write(90, 15); 
  ArmSer1.write(90, 15); 
// -------------------------- //

// LCD //
// ------------------------- //
  Wire.begin(21, 22);
  lcd.init(); 
  lcd.backlight(); 
  lcd.setCursor(0,0);
  lcd_wait();
  lcd.setCursor(0,1); 
  lcd.print("Done!");
  lcd.setCursor(0, 0);
  lcd.print(" Hive |  Frame  ");
  lcd.setCursor(5, 1);
  lcd.print(hiveNo);
  lcd.setCursor(14, 1);
  lcd.print(FrameNo);
// ------------------------- //

// Motor //
// ------------------------- //
  motor_L.begin();
  motor_R.begin();
// ------------------------- //

// Trigger Pin //
// ------------------------- //
  pinMode(TRIG_FINISH, OUTPUT);
  pinMode(TRIG_ERROR, OUTPUT);
  pinMode(TRIG_CELL, OUTPUT);
  pinMode(TRIG_BEE, OUTPUT);
// ------------------------- //

// PS5 Controller //
// ------------------------- //
  ps5.attachOnConnect([](){ Serial.println("PS5 Connected."); });
  ps5.attachOnDisconnect([](){ Serial.println("PS5 Disconnected!"); });
  ps5.begin("10:18:49:7D:6D:F4");
  Serial.println("Waiting for PS5 Controller...");
// ------------------------- //
}

void loop(){
  if (Serial.available()) { 
    char rec = Serial.read(); 
    Serial.print("Got: "); 
    Serial.println(rec);
}