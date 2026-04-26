// lcd_hello.ino — minimal I2C 1602 LCD demo for the ANEB simulator.
//
// Wiring (matches the ANEB v1.1 bench setup and the simulator):
//   PCF8574 backpack at I2C address 0x27
//   SDA = A4 (PC4), SCL = A5 (PC5)  — the AVR's TWI pins
//
// Build:
//   arduino-cli compile --fqbn arduino:avr:nano \
//                       --output-dir build \
//                       firmware/examples/lcd_hello
//   cp build/lcd_hello.ino.hex firmware/examples/lcd_hello.hex
//
// Load into any ECU from the simulator's UI ("Load firmware…") or via:
//   {"v":1,"c":"load","chip":"ecu1","path":"firmware/examples/lcd_hello.hex"}

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Hello, ANEB!");
  lcd.setCursor(0, 1);
  lcd.print("RTOS @ UFPE");
}

void loop() {
  // Simple animated tick on row 1, col 13..15. Demonstrates that the
  // simulator decodes setCursor + write_data on every iteration.
  static uint8_t tick = 0;
  lcd.setCursor(13, 1);
  lcd.print(tick % 10);
  lcd.print((tick / 10) % 10);
  lcd.print((tick / 100) % 10);
  tick++;
  delay(500);
}
