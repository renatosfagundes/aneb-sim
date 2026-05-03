// serial_echo.ino — UART read-and-echo with line counter.
//
// Demonstrates the simplest possible bidirectional UART exchange.
// Type a line into the Console (press Enter), the firmware echoes it
// back with a sequential line number prefix.  Useful for verifying
// that the bridge → COM port forwarding works in both directions
// (UI / external serial monitor → firmware RX, then firmware TX →
// bridge → COM port → display).
//
// On the ANEB v1.1 board the UART is on D0/D1 (RX/TX) at 115200 baud
// — the same pins exposed via the Nano's USB-serial converter.  In
// the simulator it shows up as the chip's Console window and as
// the bridged COM port.
//
// L LED blinks once per echoed line so the panel reflects activity.

const uint8_t L_LED = 13;

unsigned long counter = 0;
char          buf[80];
size_t        len = 0;

void setup() {
  pinMode(L_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println(F("[serial_echo] ready — type a line and press Enter"));
}

void blink() {
  digitalWrite(L_LED, HIGH);
  delay(40);
  digitalWrite(L_LED, LOW);
}

void loop() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n' || len >= sizeof(buf) - 1) {
      buf[len] = '\0';
      counter++;
      Serial.print(F("["));
      Serial.print(counter);
      Serial.print(F("] echo: "));
      Serial.println(buf);
      blink();
      len = 0;
    } else {
      buf[len++] = (char)c;
    }
  }
}
