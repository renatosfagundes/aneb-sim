// io_test.ino — exercises every ANEB v1.1 widget so the simulator UI
// can be smoke-tested end-to-end:
//
//   - LCD          : 16x2 I2C @ 0x27, shows pots + button states
//   - Trimpots     : AIN0 drives DOUT0 brightness via PWM,
//                    AIN1 drives the LDR LED brightness via PWM,
//                    AIN2/AIN3 are read and displayed on the LCD
//   - Buttons      : DIN1..DIN4 with pull-ups, current states shown
//                    on the LCD as a 4-bit bitmap
//                    DIN1 also gates DOUT1 (green LED) directly
//   - L LED (PB5)  : blinks at 2 Hz so it's clearly alive
//
// Wiring matches the ANEB v1.1 board:
//   DOUT0 = D3  (PD3, PWM via Timer 2 OC2B)
//   DOUT1 = D4  (PD4)
//   L LED = D13 (PB5, on-board)
//   LDR   = D6  (PD6, PWM via Timer 0 OC0A)
//   DIN1  = A4  (PC4, also SDA — but pulled-up reads still work)
//   DIN2  = A5  (PC5, also SCL — same)
//   DIN3  = D9  (PB1)
//   DIN4  = D8  (PB0)
//   AIN0..AIN3 = A0..A3
//
// Note: A4 and A5 are also the I2C bus, so DIN1/DIN2 share lines with
// the LCD. In the simulator the DIN events only fire when the UI
// pushes a level; the firmware's I2C traffic doesn't trip them.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const uint8_t DOUT0   = 3;
const uint8_t DOUT1   = 4;
const uint8_t L_LED   = 13;
const uint8_t LDR_LED = 6;

const uint8_t DIN1 = A4;
const uint8_t DIN2 = A5;
const uint8_t DIN3 = 9;
const uint8_t DIN4 = 8;

const uint8_t AIN0 = A0;
const uint8_t AIN1 = A1;
const uint8_t AIN2 = A2;
const uint8_t AIN3 = A3;

static unsigned long lastBlink = 0;
static bool          blinkOn   = false;

static void printPad4(int v) {
  if (v < 1000) lcd.print(' ');
  if (v < 100)  lcd.print(' ');
  if (v < 10)   lcd.print(' ');
  lcd.print(v);
}

void setup() {
  pinMode(DOUT0,   OUTPUT);
  pinMode(DOUT1,   OUTPUT);
  pinMode(L_LED,   OUTPUT);
  pinMode(LDR_LED, OUTPUT);

  pinMode(DIN1, INPUT_PULLUP);
  pinMode(DIN2, INPUT_PULLUP);
  pinMode(DIN3, INPUT_PULLUP);
  pinMode(DIN4, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("ANEB I/O test");
  lcd.setCursor(0, 1);
  lcd.print("Ready.");
  delay(800);
  lcd.clear();
}

void loop() {
  int a0 = analogRead(AIN0);
  int a1 = analogRead(AIN1);
  int a2 = analogRead(AIN2);
  int a3 = analogRead(AIN3);

  // Pots drive the LED brightnesses.
  analogWrite(DOUT0,   a0 >> 2);   // 0..1023 -> 0..255
  analogWrite(LDR_LED, a1 >> 2);

  // DIN1 gates DOUT1. Pull-ups make pressed = LOW.
  bool d1 = !digitalRead(DIN1);
  bool d2 = !digitalRead(DIN2);
  bool d3 = !digitalRead(DIN3);
  bool d4 = !digitalRead(DIN4);
  digitalWrite(DOUT1, d1 ? HIGH : LOW);

  // L LED blinks at 1 Hz (toggles every 500 ms) so the user knows
  // the loop is running without the strobe being distracting.
  unsigned long now = millis();
  if (now - lastBlink >= 500) {
    blinkOn = !blinkOn;
    digitalWrite(L_LED, blinkOn ? HIGH : LOW);
    lastBlink = now;
  }

  // LCD row 0 — first two pot values.
  lcd.setCursor(0, 0);
  lcd.print("A0:"); printPad4(a0);
  lcd.print(" A1:"); printPad4(a1);

  // LCD row 1 — DIN bitmap then last two pot values.
  lcd.setCursor(0, 1);
  lcd.print("D:");
  lcd.print(d1 ? '1' : '0');
  lcd.print(d2 ? '1' : '0');
  lcd.print(d3 ? '1' : '0');
  lcd.print(d4 ? '1' : '0');
  lcd.print(" A2:"); printPad4(a2);
  lcd.print('/');
  if (a3 < 100) lcd.print(' ');
  if (a3 < 10)  lcd.print(' ');
  lcd.print(a3 / 10);   // 0..102 — fits in 3 chars

  delay(100);    // 10 Hz refresh — readable, low simulator load
}
