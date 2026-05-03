// can_busoff_demo.ino — TX counter with live MCP2515 error-state report.
//
// Companion sketch for the engine's bus-off teaching aids.  By itself
// this firmware just transmits a counter at 10 Hz and reads back the
// MCP2515 error registers (TEC, REC, EFLG, CANSTAT) once a second.
// Real interesting behavior happens when the instructor drives errors
// from the UI:
//
//   - Toolbar → CAN → "Force bus-off on ECU2"  (or via Scenario player)
//   - Or: send {"v":1,"c":"force_busoff","chip":"ecu2"} on stdin
//
// The firmware notices the bus-off state (CANSTAT.OPMOD == 100b) and
// recovers by toggling MCP_CAN mode back to NORMAL — the same path
// real drivers use.  The Console traces every transition so students
// can watch the chip walk through active → passive → bus-off →
// recovered states.
//
// Wiring (ANEB v1.1):
//   MCP2515 CS   = D10 (PB2)
//   MCP2515 INT  = D2  (PD2)
//   SPI MOSI/MISO/SCK on the AVR's hardware SPI pins.
//
// Library: Cory J. Fowler's MCP_CAN_lib (the same one the lab uses).

#include <SPI.h>
#include <mcp_can.h>

const uint8_t CS_PIN = 10;
MCP_CAN can(CS_PIN);

const uint32_t CAN_ID  = 0x123;
const uint8_t  CAN_DLC = 4;

// Track the last reported state so we only print on transitions.
const char *stateLabel(uint8_t eflg) {
  // EFLG bits: TXBO=5, RXEP=4, RXEW=3, TXEP=2, TXEW=1, EWARN=0
  if (eflg & 0x20) return "BUS-OFF";
  if (eflg & 0x14) return "ERROR PASSIVE";
  if (eflg & 0x0B) return "ERROR WARN";
  return "ACTIVE";
}

const char *lastState = "ACTIVE";
unsigned long lastReport = 0;
uint32_t txCounter = 0;

static bool initCan() {
  return can.begin(MCP_ANY, CAN_125KBPS, MCP_8MHZ) == CAN_OK
      && can.setMode(MCP_NORMAL) == MCP_NORMAL;
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("[can_busoff_demo] starting"));
  while (!initCan()) {
    Serial.println(F("[can_busoff_demo] MCP2515 init FAILED — retrying"));
    delay(500);
  }
  Serial.println(F("[can_busoff_demo] MCP2515 ready, NORMAL mode"));
}

void loop() {
  // ---- TX counter @ 10 Hz --------------------------------------
  static unsigned long lastTx = 0;
  unsigned long now = millis();
  if (now - lastTx >= 100) {
    lastTx = now;
    uint8_t buf[CAN_DLC] = {
      (uint8_t)(txCounter >> 24), (uint8_t)(txCounter >> 16),
      (uint8_t)(txCounter >>  8), (uint8_t)(txCounter      ),
    };
    can.sendMsgBuf(CAN_ID, 0, CAN_DLC, buf);
    txCounter++;
  }

  // ---- Error-state report @ 1 Hz -------------------------------
  if (now - lastReport >= 1000) {
    lastReport = now;
    uint8_t eflg = can.getError();      // EFLG
    uint8_t tec  = can.errorCountTX();
    uint8_t rec  = can.errorCountRX();
    const char *st = stateLabel(eflg);
    Serial.print(F("tec="));    Serial.print(tec);
    Serial.print(F(" rec="));   Serial.print(rec);
    Serial.print(F(" eflg=0x"));Serial.print(eflg, HEX);
    Serial.print(F(" state=")); Serial.println(st);

    if (st != lastState) {
      Serial.print(F("[can_busoff_demo] TRANSITION: "));
      Serial.print(lastState);  Serial.print(F(" → "));
      Serial.println(st);
      lastState = st;
    }
  }

  // ---- Auto-recovery from bus-off ------------------------------
  // CANSTAT.OPMOD bits 7:5 == 100b (0x80) means LISTEN-ONLY/BUSOFF on
  // the MCP2515.  The library's getError() bit 5 (TXBO) is the simpler
  // check; on TXBO we re-init the controller — same path real drivers
  // use to clear bus-off.
  if (can.getError() & 0x20) {
    Serial.println(F("[can_busoff_demo] bus-off detected — recovering"));
    can.setMode(MCP_NORMAL);    // toggling mode clears EFLG.TXBO
    delay(50);
  }
}
