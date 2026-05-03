// loop_feedback.ino — ECU2's electrical self-feedback loop.
//
// On the ANEB v1.1 board, ECU2 has D5 (PD5) physically wired to A7,
// so a sketch can output a PWM-filtered analog level and read it
// back without external hardware.  Useful for teaching closed-loop
// control concepts: the firmware sets a target, measures the actual,
// and applies a correction.
//
// In the simulator, the engine's analog-routing layer connects PD5's
// PWM duty (after a first-order RC-filter approximation) to ADC A7
// on the same chip.
//
// This sketch implements a simple proportional controller:
//
//   1. Generate a target sweep (square wave between 30% and 70%).
//   2. Read A7 (the filtered output).
//   3. Compute error = target - measured.
//   4. Adjust duty by a fraction of the error.
//   5. Print { t_ms, target, measured, duty } as CSV for the Plotter.
//
// Run only on ECU2 — D5 / A7 wiring is ECU2-specific.
//
// Wiring:
//   LOOP_OUT = D5  (PD5, PWM via Timer 0 OC0B)
//   LOOP_IN  = A7  (PC7-equivalent ADC channel)
//   L LED    = D13 — flips on every target step

const uint8_t LOOP_OUT = 5;
const uint8_t LOOP_IN  = A7;
const uint8_t L_LED    = 13;

const int  TARGET_LO   = 1023 * 30 / 100;   // 30% of full scale
const int  TARGET_HI   = 1023 * 70 / 100;   // 70%
const unsigned long STEP_PERIOD_MS = 1500;  // square wave period
const float K_P        = 0.20f;

int   target = TARGET_LO;
int   duty   = 128;
unsigned long lastStep = 0;

void setup() {
  pinMode(LOOP_OUT, OUTPUT);
  pinMode(L_LED,    OUTPUT);
  Serial.begin(115200);
  Serial.println(F("# t_ms,target,measured,duty"));
}

void loop() {
  unsigned long now = millis();

  // Step the target every STEP_PERIOD_MS.
  if (now - lastStep >= STEP_PERIOD_MS) {
    target = (target == TARGET_LO) ? TARGET_HI : TARGET_LO;
    lastStep = now;
    digitalWrite(L_LED, target == TARGET_HI ? HIGH : LOW);
  }

  int measured = analogRead(LOOP_IN);     // 0..1023
  int error    = target - measured;       // -1023..+1023
  duty += (int)(error * K_P);
  if (duty < 0)   duty = 0;
  if (duty > 255) duty = 255;
  analogWrite(LOOP_OUT, duty);

  Serial.print(now);      Serial.print(',');
  Serial.print(target);   Serial.print(',');
  Serial.print(measured); Serial.print(',');
  Serial.println(duty);

  delay(20);   // 50 Hz control loop — fast enough for the smoothing
}
