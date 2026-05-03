// buzzer_tone.ino — drive the BUZZ pin (PD7 / D7) at audible frequencies.
//
// Demonstrates Arduino's tone() / noTone() API on the ANEB v1.1 buzzer.
// Plays a short ascending C-major arpeggio on every L-LED blink, then
// rests, so the simulator's BuzzerWidget is clearly active during the
// "play" phase and clearly silent during the "rest" phase.
//
// Wiring on the ANEB v1.1 board:
//   BUZZ  = D7  (PD7)
//   L LED = D13 (PB5, on-board)
//
// Notes (Hz, equal-temperament tuning relative to A4 = 440):
//   C5 = 523, E5 = 659, G5 = 784, C6 = 1047

const uint8_t BUZZ  = 7;
const uint8_t L_LED = 13;

const int NOTES[]      = { 523, 659, 784, 1047 };
const int N_NOTES      = sizeof(NOTES) / sizeof(NOTES[0]);
const int NOTE_MS      = 180;
const int REST_MS      = 800;

void setup() {
  pinMode(L_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println(F("[buzzer_tone] ready"));
}

void loop() {
  digitalWrite(L_LED, HIGH);
  for (int i = 0; i < N_NOTES; i++) {
    Serial.print(F("[buzzer_tone] note "));
    Serial.println(NOTES[i]);
    tone(BUZZ, NOTES[i], NOTE_MS);
    delay(NOTE_MS + 30);
  }
  noTone(BUZZ);
  digitalWrite(L_LED, LOW);
  delay(REST_MS);
}
