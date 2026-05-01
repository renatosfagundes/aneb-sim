// can_heartbeat.ino — universal CAN heartbeat firmware for ANEB v1.1 ECUs.
//
// Each board broadcasts a counter on its own CAN ID once a second and
// listens for all incoming frames.  The LCD shows the local TX counter
// on line 1 and the most recent RX (sender ID + counter value) on
// line 2.  L LED flashes on TX, DOUT0 toggles on RX — so even with the
// LCD off you can see traffic in the simulator.
//
// Build one .hex per ECU by overriding MY_ID at compile time, e.g.
//
//     arduino-cli compile --fqbn arduino:avr:nano \
//         --build-property "build.extra_flags=-DMY_ID=0x101" \
//         --output-dir build_ecu1 .
//     cp build_ecu1/can_heartbeat.ino.hex ../can_heartbeat_ecu1.hex
//
// The companion build_all.sh script produces all four .hex files
// (IDs 0x101..0x104) in one go.
//
// ANEB v1.1 wiring this firmware assumes:
//   MCP2515 CS  -> D10 (PB2)
//   MCP2515 INT -> D2  (PD2 / INT0)
//   MCP2515 SPI -> D11 / D12 / D13
//   LCD I2C     -> A4 SDA / A5 SCL @ 0x27
//   L LED       -> D13 (PB5, on-board)
//   DOUT0       -> D3  (PD3)

#include <SPI.h>
#include <mcp_can.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#ifndef MY_ID
#  define MY_ID 0x101    // overridden per ECU at compile time
#endif

constexpr uint8_t CAN_CS_PIN  = 10;
constexpr uint8_t CAN_INT_PIN = 2;
constexpr uint8_t L_LED       = 13;
constexpr uint8_t DOUT0       = 3;

MCP_CAN           can(CAN_CS_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

static uint16_t      txCount = 0;
static unsigned long lastTx  = 0;
static unsigned long ledOff  = 0;

void setup() {
    // mcp_can has DEBUG_MODE=1 by default and prints status messages
    // from inside begin() via Serial.println.  If the UART isn't
    // initialised those prints block forever waiting for UDRE — so
    // call Serial.begin() unconditionally even though the sketch
    // doesn't otherwise need it.
    Serial.begin(115200);
    pinMode(L_LED, OUTPUT);
    pinMode(DOUT0, OUTPUT);
    pinMode(CAN_INT_PIN, INPUT);

    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("CAN ID 0x");
    lcd.print(MY_ID, HEX);
    lcd.setCursor(0, 1);
    lcd.print("Init MCP2515...");

    // 16 MHz crystal, 500 kbps bus.  Retry until the controller acks.
    while (can.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK) {
        delay(200);
    }
    can.setMode(MCP_NORMAL);

    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("RX 0x--- :    0");
}

void loop() {
    unsigned long now = millis();

    // ---- TX every 1 second --------------------------------------
    if (now - lastTx >= 1000) {
        lastTx = now;
        ++txCount;
        uint8_t payload[2] = {
            (uint8_t)((txCount >> 8) & 0xFF),
            (uint8_t)( txCount       & 0xFF),
        };
        if (can.sendMsgBuf(MY_ID, 0, sizeof(payload), payload) == CAN_OK) {
            digitalWrite(L_LED, HIGH);
            ledOff = now + 80;          // 80 ms TX flash
        }

        char line[17];
        snprintf(line, sizeof(line), "0x%03X TX:%-5u", MY_ID, txCount);
        lcd.setCursor(0, 0);
        lcd.print(line);
    }

    if (ledOff != 0 && now >= ledOff) {
        digitalWrite(L_LED, LOW);
        ledOff = 0;
    }

    // ---- RX (poll the MCP2515 INT pin, active-low) --------------
    if (digitalRead(CAN_INT_PIN) == LOW) {
        long unsigned int rxId = 0;
        unsigned char     len  = 0;
        unsigned char     buf[8];
        if (can.readMsgBuf(&rxId, &len, buf) == CAN_OK) {
            uint16_t value = (len >= 2)
                                 ? ((uint16_t)buf[0] << 8 | buf[1])
                                 : 0;
            digitalWrite(DOUT0, !digitalRead(DOUT0)); // blink on each RX

            char line[17];
            snprintf(line, sizeof(line), "RX 0x%03lX:%-5u",
                     rxId & 0x7FF, value);
            lcd.setCursor(0, 1);
            lcd.print(line);
        }
    }
}
