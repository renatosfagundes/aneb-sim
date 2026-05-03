// button_counter.ino — DIN1..DIN4 increment / decrement / reset / sign,
// LCD shows the live value.
//
// Demonstrates digital-input handling with debounce + INPUT_PULLUP, and
// the 16x2 I2C LCD update path.  The simulator's DIN buttons fire as
// momentary level changes — the firmware sees a level transition that
// it has to debounce in software (10 ms is enough for the simulator's
// clean signal; real hardware would need ~30 ms).
//
// Mapping:
//   DIN1 (A4 / PC4) — increment counter (+1)
//   DIN2 (A5 / PC5) — decrement counter (-1)
//   DIN3 (D9 / PB1) — reset to zero
//   DIN4 (D8 / PB0) — toggle sign
//
//   LCD on I2C @ 0x27 — ANEB v1.1 wiring (SCL=A5/SDA=A4 shared with
//                                          DIN2/DIN1, but the I2C driver
//                                          handles the bus traffic).
//   L LED (D13)     — flashes briefly each time the counter changes.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const uint8_t DIN1   = A4;
const uint8_t DIN2   = A5;
const uint8_t DIN3   = 9;
const uint8_t DIN4   = 8;
const uint8_t L_LED  = 13;

const uint8_t DEBOUNCE_MS = 10;

struct Btn {
  uint8_t pin;
  uint8_t lastRaw;       // last raw electrical level (1 = released w/ pullup)
  uint8_t stable;        // debounced stable level
  unsigned long lastChange;
};

Btn btns[4] = {
  {DIN1, HIGH, HIGH, 0},
  {DIN2, HIGH, HIGH, 0},
  {DIN3, HIGH, HIGH, 0},
  {DIN4, HIGH, HIGH, 0},
};

long counter = 0;
int  sign_   = 1;

static void renderLcd() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Counter:"));
  lcd.setCursor(0, 1);
  long shown = sign_ * counter;
  if (shown < 0) {
    lcd.print('-');
    shown = -shown;
  } else {
    lcd.print(' ');
  }
  lcd.print(shown);
}

// Returns true if the button just transitioned released → pressed (a
// "fresh" click event), false otherwise.  Pull-ups invert: pressed = LOW.
static bool justPressed(Btn &b, unsigned long now) {
  uint8_t raw = digitalRead(b.pin);
  if (raw != b.lastRaw) {
    b.lastChange = now;
    b.lastRaw    = raw;
  }
  if ((now - b.lastChange) < DEBOUNCE_MS) return false;
  if (raw != b.stable) {
    bool wasPressed = (b.stable == LOW);
    b.stable = raw;
    bool isPressed = (raw == LOW);
    return !wasPressed && isPressed;     // released → pressed
  }
  return false;
}

void setup() {
  pinMode(DIN1, INPUT_PULLUP);
  pinMode(DIN2, INPUT_PULLUP);
  pinMode(DIN3, INPUT_PULLUP);
  pinMode(DIN4, INPUT_PULLUP);
  pinMode(L_LED, OUTPUT);

  lcd.init();
  lcd.backlight();
  Serial.begin(115200);
  renderLcd();
  Serial.println(F("[button_counter] ready — DIN1=+1 DIN2=-1 DIN3=reset DIN4=sign"));
}

void loop() {
  unsigned long now = millis();
  bool changed = false;

  if (justPressed(btns[0], now)) { counter++;     changed = true; }
  if (justPressed(btns[1], now)) { counter--;     changed = true; }
  if (justPressed(btns[2], now)) { counter = 0;   changed = true; }
  if (justPressed(btns[3], now)) { sign_  = -sign_; changed = true; }

  if (changed) {
    digitalWrite(L_LED, HIGH);
    renderLcd();
    Serial.print(F("counter="));
    Serial.println(sign_ * counter);
  }

  // Drop the L-LED 50 ms after the last edge; cheap but works.
  static unsigned long ledOffAt = 0;
  if (changed)        ledOffAt = now + 50;
  if (now >= ledOffAt) digitalWrite(L_LED, LOW);

  delay(2);    // ≈500 Hz scan; debounce window is the real filter
}
