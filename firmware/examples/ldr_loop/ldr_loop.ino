// ldr_loop.ino — ECU1's optical closed-loop demo.
//
// On the real ANEB v1.1 board, ECU1 has an LDR (light-dependent
// resistor) physically pointed at the LDR_LED.  The firmware drives
// the LED's brightness via PWM and reads the LDR's voltage on A0;
// the resulting curve is a (noisy) optical-coupling transfer function.
//
// In the simulator the optical path is modeled in the engine's
// analog-routing layer (sim_loop.c), so the same sketch behaves
// the same way: increase PD6's duty cycle → A0 climbs.
//
// The sketch sweeps PD6 in a triangle wave from 0 to 255 and back,
// reads A0 at each step, and prints both as CSV so the Plotter (or
// remote_flasher's Plotter tab) can render the response curve.
//
// Wiring:
//   LDR_LED = D6 (PD6, PWM via Timer 0 OC0A)
//   LDR     = A0 (PC0 — the AIN0 trim-pot's slot on ECU1's panel)
//   L LED   = D13 — flashes once per full sweep
//
// Tip: open the Plotter and enable both `pwm:PD6` and `adc:0` to see
// the LED command and the LDR response on the same axes.

const uint8_t LDR_LED = 6;
const uint8_t LDR_IN  = A0;
const uint8_t L_LED   = 13;

const uint8_t STEP    = 4;       // 0..252..0 = 64 + 64 steps per sweep
const uint16_t HOLD_MS = 30;     // settle time at each PWM level

void setup() {
  pinMode(LDR_LED, OUTPUT);
  pinMode(L_LED,   OUTPUT);
  Serial.begin(115200);
  Serial.println(F("# t_ms,duty,ldr"));
}

void loop() {
  digitalWrite(L_LED, HIGH);
  for (int duty = 0; duty <= 252; duty += STEP) {
    analogWrite(LDR_LED, duty);
    delay(HOLD_MS);
    int v = analogRead(LDR_IN);
    Serial.print(millis()); Serial.print(',');
    Serial.print(duty);     Serial.print(',');
    Serial.println(v);
  }
  digitalWrite(L_LED, LOW);
  for (int duty = 252; duty >= 0; duty -= STEP) {
    analogWrite(LDR_LED, duty);
    delay(HOLD_MS);
    int v = analogRead(LDR_IN);
    Serial.print(millis()); Serial.print(',');
    Serial.print(duty);     Serial.print(',');
    Serial.println(v);
  }
}
