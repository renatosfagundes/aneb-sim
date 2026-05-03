// mcu_mode_select.ino — minimal state machine on the ATmega328PB MCU.
//
// The MCU chip on the ANEB v1.1 board doesn't sit on the CAN bus;
// it's the board controller, with two mode-selector switches on
// D8 and D9 that the operator uses to pick which lab scenario the
// rest of the board runs.  This sketch demonstrates a 4-state FSM
// driven by those two switches and reports state transitions on
// the LCD + UART.
//
// Mapping (from the silkscreen on the panel):
//   Mode 1 = D8 (PB0)
//   Mode 2 = D9 (PB1)
//   L LED  = D13 (PB5) — flashes once per state change
//
// State table (Mode2, Mode1):
//   00 → ELECTRIC      (electric drivetrain demo)
//   01 → AUTO          (combustion automatic)
//   10 → MANUAL        (combustion manual)
//   11 → DIAGNOSTIC    (DTC injection + dashboard warning lights)
//
// Run only on the MCU chip.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const uint8_t MODE1 = 8;
const uint8_t MODE2 = 9;
const uint8_t L_LED = 13;

enum Mode {
  MODE_ELECTRIC = 0,
  MODE_AUTO     = 1,
  MODE_MANUAL   = 2,
  MODE_DIAG     = 3,
};

const char *MODE_NAMES[] = {
  "ELECTRIC",
  "AUTO    ",
  "MANUAL  ",
  "DIAG    ",
};

static Mode readMode() {
  // Buttons are INPUT_PULLUP — pressed = LOW.  In the simulator they
  // toggle on click, so the level reflects whatever the user last set.
  uint8_t m1 = digitalRead(MODE1) == LOW ? 1 : 0;
  uint8_t m2 = digitalRead(MODE2) == LOW ? 1 : 0;
  return (Mode)((m2 << 1) | m1);
}

Mode current = MODE_ELECTRIC;

static void renderLcd(Mode m) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("MCU mode:"));
  lcd.setCursor(0, 1);
  lcd.print(MODE_NAMES[m]);
}

void setup() {
  pinMode(MODE1, INPUT_PULLUP);
  pinMode(MODE2, INPUT_PULLUP);
  pinMode(L_LED, OUTPUT);
  lcd.init();
  lcd.backlight();
  Serial.begin(115200);

  current = readMode();
  renderLcd(current);
  Serial.print(F("[mcu_mode_select] booted in mode "));
  Serial.println(MODE_NAMES[current]);
}

void loop() {
  Mode m = readMode();
  if (m != current) {
    current = m;
    renderLcd(m);
    digitalWrite(L_LED, HIGH);
    Serial.print(F("[mcu_mode_select] mode → "));
    Serial.println(MODE_NAMES[m]);
    delay(60);
    digitalWrite(L_LED, LOW);
  }
  delay(20);
}
