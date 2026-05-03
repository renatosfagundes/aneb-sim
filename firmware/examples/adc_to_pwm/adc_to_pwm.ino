// adc_to_pwm.ino — read AIN0 (trim-pot) and drive DOUT0 brightness.
//
// Smallest "input → analog output" example.  Turn the AIN0 pot in the
// simulator UI; DOUT0 (PWM on PD3) tracks it linearly.  The Plotter
// shows both signals: enable `adc:0` (AIN0 raw 0..1023) and `pwm:PD3`
// (DOUT0 duty 0.0..1.0) and you'll see them as parallel curves.
//
// Wiring:
//   AIN0  = A0 (PC0)
//   DOUT0 = D3 (PD3, PWM via Timer 2 OC2B)
//   L LED = D13 — heartbeat so the loop is visibly alive

const uint8_t AIN0  = A0;
const uint8_t DOUT0 = 3;
const uint8_t L_LED = 13;

void setup() {
  pinMode(DOUT0, OUTPUT);
  pinMode(L_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println(F("[adc_to_pwm] ready"));
}

void loop() {
  int v = analogRead(AIN0);     // 0..1023
  analogWrite(DOUT0, v >> 2);   // → 0..255 PWM duty

  // Heartbeat at 1 Hz, independent of the loop's main work.
  static unsigned long lastBlink = 0;
  static bool          on        = false;
  unsigned long now = millis();
  if (now - lastBlink >= 500) {
    on = !on;
    digitalWrite(L_LED, on ? HIGH : LOW);
    lastBlink = now;

    // Telemetry once per blink so the Console isn't drowned at 100 Hz.
    Serial.print(F("AIN0="));
    Serial.print(v);
    Serial.print(F(" duty="));
    Serial.println(v >> 2);
  }
  delay(10);   // 100 Hz sample rate; PWM is hardware so duty stays smooth
}
