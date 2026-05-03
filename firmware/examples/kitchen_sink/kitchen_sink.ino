// kitchen_sink.ino — exercise every peripheral on the ANEB v1.1 board.
//
// "Smoke test that humans can read."  Touches GPIO out (DOUT0/1, L,
// LDR_LED), GPIO in (DIN1..DIN4 with debounce), ADC (AIN0..AIN3 + the
// LDR feedback on A0 if running on ECU1), PWM (DOUT0 dimmable from
// AIN0, LDR_LED dimmable from AIN1), UART (status print), I²C LCD,
// buzzer (one beep on each DIN1 click), and CAN (broadcast a frame
// every second carrying the four ADC readings).
//
// Intended to flash on any ECU1..ECU4.  The CAN frame carries the
// chip's own AIN0..AIN3 readings; if you flash all four ECUs with
// this sketch you get a four-node CAN demo where each chip reports
// its panel state to the others — see can_heartbeat for the smaller
// targeted variant.
//
// Wiring:
//   DOUT0   = D3  (PD3, PWM)        DIN1  = A4 (PC4, INPUT_PULLUP)
//   DOUT1   = D4  (PD4)             DIN2  = A5 (PC5, INPUT_PULLUP)
//   L LED   = D13 (PB5)             DIN3  = D9 (PB1, INPUT_PULLUP)
//   LDR_LED = D6  (PD6, PWM)        DIN4  = D8 (PB0, INPUT_PULLUP)
//   BUZZ    = D7  (PD7, tone)
//   AIN0..3 = A0..A3
//   MCP2515 CS = D10, INT = D2

#include <SPI.h>
#include <Wire.h>
#include <mcp_can.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
MCP_CAN can(10);

const uint8_t DOUT0   = 3;
const uint8_t DOUT1   = 4;
const uint8_t L_LED   = 13;
const uint8_t LDR_LED = 6;
const uint8_t BUZZ    = 7;
const uint8_t DIN1    = A4;
const uint8_t DIN2    = A5;
const uint8_t DIN3    = 9;
const uint8_t DIN4    = 8;

const uint32_t CAN_ID = 0x100;

bool prevDin1 = HIGH;

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
  Serial.begin(115200);
  Serial.println(F("[kitchen_sink] booting"));

  if (can.begin(MCP_ANY, CAN_125KBPS, MCP_8MHZ) == CAN_OK) {
    can.setMode(MCP_NORMAL);
    Serial.println(F("[kitchen_sink] CAN ready"));
  } else {
    Serial.println(F("[kitchen_sink] CAN init failed — continuing without CAN"));
  }

  lcd.setCursor(0, 0);
  lcd.print(F("kitchen_sink"));
  lcd.setCursor(0, 1);
  lcd.print(F("ready"));
  delay(600);
}

void loop() {
  unsigned long now = millis();

  // ---- Inputs --------------------------------------------------
  int a0 = analogRead(A0);
  int a1 = analogRead(A1);
  int a2 = analogRead(A2);
  int a3 = analogRead(A3);
  bool d1 = digitalRead(DIN1) == LOW;
  bool d2 = digitalRead(DIN2) == LOW;
  bool d3 = digitalRead(DIN3) == LOW;
  bool d4 = digitalRead(DIN4) == LOW;

  // ---- Outputs driven by inputs --------------------------------
  analogWrite(DOUT0,   a0 >> 2);   // AIN0 → DOUT0 brightness
  analogWrite(LDR_LED, a1 >> 2);   // AIN1 → LDR_LED brightness
  digitalWrite(DOUT1,  d2 ? HIGH : LOW);

  // ---- L-LED 1 Hz heartbeat ------------------------------------
  static unsigned long lastBlink = 0;
  static bool          on        = false;
  if (now - lastBlink >= 500) {
    on = !on;
    digitalWrite(L_LED, on ? HIGH : LOW);
    lastBlink = now;
  }

  // ---- BUZZ on DIN1 rising edge --------------------------------
  if (d1 && !prevDin1)        tone(BUZZ, 1200, 80);
  prevDin1 = d1;

  // ---- LCD refresh @ 10 Hz -------------------------------------
  static unsigned long lastLcd = 0;
  if (now - lastLcd >= 100) {
    lastLcd = now;
    lcd.setCursor(0, 0);
    lcd.print(F("A0:"));   printPad4(a0);
    lcd.print(F(" A1:")); printPad4(a1);
    lcd.setCursor(0, 1);
    lcd.print(F("D:"));
    lcd.print(d1 ? '1' : '0');
    lcd.print(d2 ? '1' : '0');
    lcd.print(d3 ? '1' : '0');
    lcd.print(d4 ? '1' : '0');
    lcd.print(F(" A2:")); printPad4(a2);
  }

  // ---- CAN broadcast @ 1 Hz ------------------------------------
  static unsigned long lastCan = 0;
  if (now - lastCan >= 1000) {
    lastCan = now;
    uint8_t buf[8] = {
      (uint8_t)(a0 >> 2), (uint8_t)(a1 >> 2),
      (uint8_t)(a2 >> 2), (uint8_t)(a3 >> 2),
      (uint8_t)((d1 << 0) | (d2 << 1) | (d3 << 2) | (d4 << 3)),
      0, 0, 0,
    };
    can.sendMsgBuf(CAN_ID, 0, 8, buf);
    Serial.print(F("tx ")); Serial.print(a0);
    Serial.print(',');      Serial.print(a1);
    Serial.print(',');      Serial.print(a2);
    Serial.print(',');      Serial.println(a3);
  }

  delay(10);
}
