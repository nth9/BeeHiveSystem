#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ps5Controller.h>

// LCD (I2C addr might be 0x27 or 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Hive & Station numbers
int hiveNo = 1;
int stationNo = 1;

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hive No.|Station");
  lcd.setCursor(0, 1);
  lcd.printf("   %d    |   %d   ", hiveNo, stationNo);
}

void setup() {
  // Start LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  updateLCD();

  // Start PS5 Controller
  ps5.begin("01:02:03:04:05:06"); // <- replace with your ESP32 BT MAC
}

void loop() {
  if (ps5.isConnected()) {
    // --- Change Hive No. ---
    if (ps5.Up()) {
      hiveNo++;
      if (hiveNo > 99) hiveNo = 1; // wrap around
      updateLCD();
      delay(250); // debounce
    }
    if (ps5.Down()) {
      hiveNo--;
      if (hiveNo < 1) hiveNo = 99; // wrap around
      updateLCD();
      delay(250);
    }

    // --- Change Station No. ---
    if (ps5.Left()) {
      stationNo--;
      if (stationNo < 1) stationNo = 9;
      updateLCD();
      delay(250);
    }
    if (ps5.Right()) {
      stationNo++;
      if (stationNo > 9) stationNo = 1;
      updateLCD();
      delay(250);
    }
  }
}
