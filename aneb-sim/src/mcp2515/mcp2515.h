/*
 * mcp2515.h — pure-logic model of the Microchip MCP2515 CAN controller.
 *
 * Deliberately free of any simavr dependency: the model is driven by
 * three external stimuli — CS edges, SPI bytes, and (later) inbound
 * bus frames — and exposes two outputs via callbacks: INT-pin level
 * changes and outbound bus frames.
 *
 * The simavr glue in sim_loop.c bridges this model to one AVR core's
 * SPI peripheral, GPIO chip-select, and external interrupt input.
 *
 * Tests link this module directly without simavr.
 */
#ifndef ANEB_MCP2515_H
#define ANEB_MCP2515_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mcp2515_regs.h"

/* ----- Frame --------------------------------------------------------- */

typedef struct mcp2515_frame {
    uint32_t id;            /* 11- or 29-bit identifier (LSB-aligned) */
    bool     ext;           /* extended (29-bit) frame */
    bool     rtr;           /* remote-transmission-request */
    uint8_t  dlc;           /* data length 0..8 */
    uint8_t  data[8];
} mcp2515_frame_t;

/* ----- Callbacks ----------------------------------------------------- */

/* Called when the INT pin level changes.
 * `asserted` = 1 means the pin should be driven LOW (active-low interrupt).
 * `asserted` = 0 means the pin should be released HIGH.
 */
typedef void (*mcp2515_int_cb)(void *ctx, int asserted);

/* Called when the controller transmits a frame onto the CAN bus.
 * Used by the bus model in M3+. In M2 / loopback this callback is not
 * invoked because the frame stays inside the controller. */
typedef void (*mcp2515_tx_cb)(void *ctx, const mcp2515_frame_t *frame);

/* ----- SPI transaction state machine --------------------------------- */

typedef enum {
    MCP_TXN_IDLE = 0,
    MCP_TXN_NEED_ADDR,      /* READ/WRITE/BIT MODIFY: collecting address */
    MCP_TXN_NEED_MASK,      /* BIT MODIFY: collecting mask */
    MCP_TXN_NEED_DATA,      /* WRITE / BIT-MODIFY-final: data byte(s) */
    MCP_TXN_READ_DATA,      /* READ: emitting register bytes */
    MCP_TXN_LOAD_TX,        /* LOAD TX BUFFER: filling buffer */
    MCP_TXN_READ_RX,        /* READ RX BUFFER: emitting buffer */
    MCP_TXN_STATUS,         /* READ STATUS: emit & repeat */
    MCP_TXN_RX_STATUS,      /* RX STATUS:  emit & repeat */
    MCP_TXN_DONE,           /* command consumed; further bytes ignored */
} mcp2515_txn_state_t;

/* ----- Controller ---------------------------------------------------- */

typedef struct mcp2515 {
    /* Public — set before mcp2515_init */
    mcp2515_int_cb on_int;
    mcp2515_tx_cb  on_tx;
    void          *ctx;
    char           id[16];      /* diagnostics, e.g. "ecu1.can1" */

    /* Internal — do not poke directly. Public so tests can inspect. */
    uint8_t  regs[MCP_REGS_SIZE];

    int      mode;              /* MCP_MODE_* */

    /* SPI transaction state */
    bool                 cs_low;
    int                  txn_pos;       /* byte index since CS fell */
    uint8_t              txn_cmd;
    uint8_t              txn_addr;
    uint8_t              txn_mask;
    mcp2515_txn_state_t  txn_state;
    int                  load_tx_buf;   /* 0/1/2 — which TXB target */
    int                  load_tx_off;   /* byte offset within target */
    int                  read_rx_buf;   /* 0/1 */
    int                  read_rx_off;
    bool                 read_rx_data_only; /* 0x92/0x96 vs 0x90/0x94 */

    /* INT pin shadow — track to coalesce identical edges */
    bool int_asserted;
} mcp2515_t;

/* ----- Lifecycle ----------------------------------------------------- */

void mcp2515_init (mcp2515_t *m, const char *id);
void mcp2515_reset(mcp2515_t *m);   /* RESET command behavior */

/* ----- SPI driver-side --------------------------------------------- */

void    mcp2515_cs_low (mcp2515_t *m);
void    mcp2515_cs_high(mcp2515_t *m);
uint8_t mcp2515_spi_byte(mcp2515_t *m, uint8_t in);

/* ----- Bus side (used by M3 bus + M2 tests) ----------------------- */

/* Inject a frame as if it had arrived from the CAN bus. Filtering, RX
 * routing, RXnIF assertion, INT pin update happen inside.
 * Returns true if the frame was accepted (filtered into a buffer);
 * false if dropped (no matching filter and not in RXM=any mode). */
bool mcp2515_rx_frame(mcp2515_t *m, const mcp2515_frame_t *frame);

/* ----- Error counters / EFLG / bus-off (M4) ---------------------- */

typedef enum {
    MCP_ERR_STATE_ACTIVE  = 0,  /* TEC < 128 && REC < 128 */
    MCP_ERR_STATE_PASSIVE = 1,  /* TEC >= 128 || REC >= 128 */
    MCP_ERR_STATE_BUSOFF  = 2,  /* TEC >= 256 (TX disabled) */
} mcp2515_err_state_t;

mcp2515_err_state_t mcp2515_err_state(const mcp2515_t *m);
bool                mcp2515_is_busoff(const mcp2515_t *m);
uint8_t             mcp2515_tec      (const mcp2515_t *m);
uint8_t             mcp2515_rec      (const mcp2515_t *m);

/* Inject error events. count is the number of "error increments" — TEC
 * gains 8 per TX-error count, REC gains 1 per RX-error count, matching
 * datasheet section 6.2. After each call, EFLG and the error state
 * (active/passive/bus-off) update automatically; CANINTF.ERRIF and
 * MERRF are set when applicable, which may assert the INT pin. */
void mcp2515_inject_tx_errors(mcp2515_t *m, int count);
void mcp2515_inject_rx_errors(mcp2515_t *m, int count);

/* Force the controller directly into a state — handy for instructor
 * demos and scenario scripts. */
void mcp2515_force_busoff      (mcp2515_t *m);
void mcp2515_force_error_passive(mcp2515_t *m);

/* Bus-off recovery: clears the bus-off state and resets counters.
 * Real hardware requires 128 occurrences of 11 consecutive recessive
 * bits OR a mode transition through Configuration; we model the
 * latter (firmware path is identical: write CANCTRL.REQOP). */
void mcp2515_recover_busoff(mcp2515_t *m);

/* ----- Inspection (for tests / diagnostics) ----------------------- */

uint8_t mcp2515_reg_read (const mcp2515_t *m, uint8_t addr);
void    mcp2515_reg_write(mcp2515_t *m, uint8_t addr, uint8_t value);
int     mcp2515_get_mode (const mcp2515_t *m);

#endif /* ANEB_MCP2515_H */
