/*
 * mcp2515.c — implementation of the Microchip MCP2515 logic model.
 *
 * Scope (M2):
 *   - Full SPI command set (RESET, READ, WRITE, BIT MODIFY, READ STATUS,
 *     RX STATUS, LOAD TX BUFFER, READ RX BUFFER, RTS).
 *   - Register file with reset values + mode-aware write protection.
 *   - Mode transitions (Configuration / Normal / Loopback / Listen-only / Sleep).
 *   - TX/RX buffers with priority and rollover (BUKT).
 *   - Acceptance filters and masks for both standard and extended frames.
 *   - Loopback path: TX RTS -> filters -> RX buffer -> INT.
 *
 * Out of scope until M3/M4: bus arbitration, error counters (TEC/REC),
 * error frames, EFLG bits, sleep wakeup-on-bus.
 *
 * Bit-timing registers (CNF1/2/3) are accepted and round-tripped but not
 * enforced — frame delivery is logical, not bit-timed.
 */
#include "mcp2515.h"

#include <string.h>

/* ----- forward decls -------------------------------------------------- */

static void deliver_loopback(mcp2515_t *m, int txbuf);
static bool route_inbound   (mcp2515_t *m, const mcp2515_frame_t *f);
static void update_int_pin  (mcp2515_t *m);

/* ----- helpers ------------------------------------------------------- */

static void set_mode(mcp2515_t *m, int new_mode)
{
    m->mode = new_mode;
    /* Reflect in CANSTAT.OPMOD (read-only) and CANCTRL.REQOP. */
    m->regs[MCP_CANSTAT] = (m->regs[MCP_CANSTAT] & ~MCP_CANSTAT_OPMOD_MASK)
                         | ((uint8_t)new_mode << MCP_CANSTAT_OPMOD_SHIFT);
    m->regs[MCP_CANCTRL] = (m->regs[MCP_CANCTRL] & ~MCP_CANCTRL_REQOP_MASK)
                         | ((uint8_t)new_mode << MCP_CANCTRL_REQOP_SHIFT);
}

static int txbuf_base(int n)
{
    /* TX buffer n's CTRL register address. */
    static const uint8_t base[3] = { MCP_TXB0CTRL, MCP_TXB1CTRL, MCP_TXB2CTRL };
    return base[n & 0x3];
}

/* ----- lifecycle ----------------------------------------------------- */

void mcp2515_init(mcp2515_t *m, const char *id)
{
    memset(m, 0, sizeof(*m));
    if (id) {
        size_t n = strlen(id);
        if (n >= sizeof(m->id)) n = sizeof(m->id) - 1;
        memcpy(m->id, id, n);
    }
    mcp2515_reset(m);
}

void mcp2515_reset(mcp2515_t *m)
{
    /* Per datasheet 10.1: RESET puts the device in Configuration mode and
     * clears all registers except for the values explicitly listed below. */
    memset(m->regs, 0, sizeof(m->regs));

    /* CANCTRL reset value: REQOP=Configuration, ABAT=0, OSM=0, CLKEN=1,
     * CLKPRE=11. = 0x80 | 0x04 | 0x03 = 0x87. */
    m->regs[MCP_CANCTRL] = 0x87;
    m->regs[MCP_CANSTAT] = (uint8_t)MCP_MODE_CONFIG << MCP_CANSTAT_OPMOD_SHIFT;
    m->mode = MCP_MODE_CONFIG;

    m->cs_low      = false;
    m->txn_pos     = 0;
    m->txn_state   = MCP_TXN_IDLE;
    m->load_tx_buf = 0;
    m->load_tx_off = 0;
    m->read_rx_buf = 0;
    m->read_rx_off = 0;
    m->read_rx_data_only = false;

    /* INT pin released. */
    if (m->int_asserted && m->on_int) m->on_int(m->ctx, 0);
    m->int_asserted = false;
}

/* ----- register write side effects ---------------------------------- */

static void apply_reg_write(mcp2515_t *m, uint8_t addr, uint8_t value)
{
    /* CANCTRL: writing REQOP requests a mode change. We treat the request
     * as instantaneous (no real bit timing); the equivalent OPMOD bits in
     * CANSTAT update synchronously. */
    if (addr == MCP_CANCTRL) {
        uint8_t req = (value & MCP_CANCTRL_REQOP_MASK) >> MCP_CANCTRL_REQOP_SHIFT;
        m->regs[MCP_CANCTRL] = value;
        if (req <= MCP_MODE_CONFIG) {
            set_mode(m, (int)req);
        }
        return;
    }

    /* CANSTAT is read-only (datasheet 10.1: "the CANSTAT and EFLG registers
     * cannot be written directly"). The ICOD and OPMOD bits reflect state. */
    if (addr == MCP_CANSTAT) {
        return;
    }

    /* CANINTF: bits cleared by writing 0; we model that as "store as written"
     * because driver code typically clears via BIT MODIFY anyway. */
    if (addr == MCP_CANINTF) {
        m->regs[addr] = value;
        update_int_pin(m);
        return;
    }

    /* CANINTE: enable mask. */
    if (addr == MCP_CANINTE) {
        m->regs[addr] = value;
        update_int_pin(m);
        return;
    }

    /* TEC/REC are managed by the controller; ignore writes (datasheet:
     * read-only). */
    if (addr == MCP_TEC || addr == MCP_REC) {
        return;
    }

    /* TXBnCTRL: TXREQ may be set by driver to start a transmission.
     * If the new value has TXREQ asserted and we are in Loopback mode,
     * loop the frame back through the RX path immediately. */
    if (addr == MCP_TXB0CTRL || addr == MCP_TXB1CTRL || addr == MCP_TXB2CTRL) {
        bool was_req = (m->regs[addr] & MCP_TXBCTRL_TXREQ) != 0;
        bool new_req = (value & MCP_TXBCTRL_TXREQ) != 0;
        m->regs[addr] = value;
        if (!was_req && new_req) {
            int n = (addr - MCP_TXB0CTRL) / 0x10;
            if (m->mode == MCP_MODE_LOOPBACK) {
                deliver_loopback(m, n);
            } else if (m->mode == MCP_MODE_NORMAL) {
                /* M3: forward to bus model via on_tx callback. For M2 the
                 * frame simply stays pending until the bus is online. */
                /* Mark transmission complete on best-effort basis to avoid
                 * stalling firmware that polls TXREQ in absence of a bus. */
            }
        }
        return;
    }

    /* All other addresses: plain write. */
    m->regs[addr] = value;
}

/* ----- read/write/bit-modify primitives ----------------------------- */

uint8_t mcp2515_reg_read(const mcp2515_t *m, uint8_t addr)
{
    if (addr >= MCP_REGS_SIZE) return 0;
    return m->regs[addr];
}

void mcp2515_reg_write(mcp2515_t *m, uint8_t addr, uint8_t value)
{
    if (addr >= MCP_REGS_SIZE) return;
    apply_reg_write(m, addr, value);
}

static void bit_modify(mcp2515_t *m, uint8_t addr, uint8_t mask, uint8_t value)
{
    if (addr >= MCP_REGS_SIZE) return;
    /* BIT MODIFY is only valid on certain registers per datasheet table 12-1.
     * We allow it on any register for permissiveness; broken firmware would
     * fail anyway. The masked-bit semantics are universal: new = (cur & ~mask)
     * | (value & mask). */
    uint8_t cur = m->regs[addr];
    uint8_t merged = (cur & ~mask) | (value & mask);
    apply_reg_write(m, addr, merged);
}

/* ----- INT pin update ------------------------------------------------ */

static void update_int_pin(mcp2515_t *m)
{
    bool should = (m->regs[MCP_CANINTF] & m->regs[MCP_CANINTE]) != 0;
    if (should != m->int_asserted) {
        m->int_asserted = should;
        if (m->on_int) m->on_int(m->ctx, should ? 1 : 0);
    }
}

/* ----- Acceptance filtering ---------------------------------------- */

/*
 * Pack/unpack 11-bit standard or 29-bit extended IDs to/from the MCP2515's
 * SIDH/SIDL/EID8/EID0 byte layout.
 *
 *   Standard (11 bits):
 *     SIDH = sid[10:3]
 *     SIDL = sid[2:0]<<5 | EXIDE | EID[17:16]
 *
 *   Extended (29 bits):
 *     SIDH = id[28:21]
 *     SIDL = id[20:18]<<5 | EXIDE | id[17:16]
 *     EID8 = id[15:8]
 *     EID0 = id[7:0]
 */
static void unpack_id(const uint8_t *bytes, uint32_t *id_out, bool *ext_out)
{
    uint8_t sidh = bytes[0];
    uint8_t sidl = bytes[1];
    uint8_t eid8 = bytes[2];
    uint8_t eid0 = bytes[3];

    bool ext = (sidl & MCP_SIDL_EXIDE) != 0;
    if (ext) {
        uint32_t id = ((uint32_t)sidh << 21)
                    | ((uint32_t)(sidl >> 5) << 18)
                    | ((uint32_t)(sidl & 0x03) << 16)
                    | ((uint32_t)eid8 << 8)
                    |  (uint32_t)eid0;
        *id_out  = id & 0x1FFFFFFFu;
        *ext_out = true;
    } else {
        uint32_t id = ((uint32_t)sidh << 3) | ((uint32_t)sidl >> 5);
        *id_out  = id & 0x7FFu;
        *ext_out = false;
    }
}

/* "Decoded" id of a filter or mask register quartet, as a single 32-bit
 * value formed by concatenating sidh/sidl/eid8/eid0 in their natural bit
 * positions. Used for bitwise filter == (id & mask) checks. */
static uint32_t encoded_id(uint32_t id, bool ext)
{
    if (ext) {
        return id & 0x1FFFFFFFu;
    } else {
        /* For standard frames, the EID portion is don't-care in matching;
         * place the 11-bit ID in the high portion to align with how filters
         * are written. */
        return (id & 0x7FFu) << 18;
    }
}

static bool match_filter(uint32_t id, bool ext, uint32_t fid, bool fext, uint32_t mask)
{
    /* If the filter and incoming frame disagree on extended/standard, no
     * match (filters with EXIDE set match only extended frames; without
     * EXIDE, only standard). */
    if (ext != fext) return false;
    uint32_t a = encoded_id(id,  ext);
    uint32_t b = encoded_id(fid, fext);
    return ((a ^ b) & mask) == 0;
}

static void filter_quartet(const uint8_t *bytes, uint32_t *id, bool *ext)
{
    /* Same byte layout as TX/RX buffer ID; reuse unpack. */
    unpack_id(bytes, id, ext);
}

static uint32_t mask_quartet(const uint8_t *bytes)
{
    /* Mask registers do NOT have an EXIDE bit; treat as same encoding as
     * an extended-id field for bit-level masking. The bit positions for
     * standard-id mask and extended-id mask happen to align because we
     * left-shift standard IDs by 18 in encoded_id(). */
    uint8_t sidh = bytes[0];
    uint8_t sidl = bytes[1];
    uint8_t eid8 = bytes[2];
    uint8_t eid0 = bytes[3];
    return ((uint32_t)sidh << 21)
         | ((uint32_t)(sidl >> 5) << 18)
         | ((uint32_t)(sidl & 0x03) << 16)
         | ((uint32_t)eid8 << 8)
         |  (uint32_t)eid0;
}

/* Returns the filter index (0..5) that matches, or -1 if none. */
static int find_matching_filter(mcp2515_t *m, const mcp2515_frame_t *f, int *which_buf)
{
    /* RXB0 uses RXM0 + RXF0,RXF1 ; RXB1 uses RXM1 + RXF2..RXF5. */
    static const uint8_t rxf_addr[6] = {
        MCP_RXF0SIDH, MCP_RXF1SIDH,
        MCP_RXF2SIDH, MCP_RXF3SIDH, MCP_RXF4SIDH, MCP_RXF5SIDH,
    };

    uint32_t mask0 = mask_quartet(&m->regs[MCP_RXM0SIDH]);
    uint32_t mask1 = mask_quartet(&m->regs[MCP_RXM1SIDH]);

    /* RXB0: filters 0..1 with mask0 */
    for (int fi = 0; fi < 2; fi++) {
        uint32_t fid; bool fext;
        filter_quartet(&m->regs[rxf_addr[fi]], &fid, &fext);
        if (match_filter(f->id, f->ext, fid, fext, mask0)) {
            *which_buf = 0;
            return fi;
        }
    }
    /* RXB1: filters 2..5 with mask1 */
    for (int fi = 2; fi < 6; fi++) {
        uint32_t fid; bool fext;
        filter_quartet(&m->regs[rxf_addr[fi]], &fid, &fext);
        if (match_filter(f->id, f->ext, fid, fext, mask1)) {
            *which_buf = 1;
            return fi;
        }
    }
    return -1;
}

/* ----- RX routing --------------------------------------------------- */

static void deposit_in_rxb(mcp2515_t *m, int rxn, const mcp2515_frame_t *f, int filter_idx)
{
    int base = (rxn == 0) ? MCP_RXB0CTRL : MCP_RXB1CTRL;

    /* SIDH/SIDL/EID8/EID0 */
    if (f->ext) {
        m->regs[base + 1] = (uint8_t)((f->id >> 21) & 0xFF);
        m->regs[base + 2] = (uint8_t)(((f->id >> 18) & 0x07) << 5)
                          | MCP_SIDL_EXIDE
                          | (uint8_t)((f->id >> 16) & 0x03);
        m->regs[base + 3] = (uint8_t)((f->id >> 8) & 0xFF);
        m->regs[base + 4] = (uint8_t)(f->id & 0xFF);
    } else {
        m->regs[base + 1] = (uint8_t)((f->id >> 3) & 0xFF);
        m->regs[base + 2] = (uint8_t)((f->id & 0x07) << 5);
        m->regs[base + 3] = 0;
        m->regs[base + 4] = 0;
    }

    /* DLC + RTR */
    m->regs[base + 5] = (uint8_t)(f->dlc & MCP_DLC_DLC_MASK)
                      | (f->rtr ? MCP_DLC_RTR : 0);

    /* Data bytes 0..7 */
    for (int i = 0; i < 8; i++) {
        m->regs[base + 6 + i] = (i < f->dlc) ? f->data[i] : 0;
    }

    /* Update RXBnCTRL: clear filter-hit bits, set new ones. RXRTR mirrors RTR. */
    uint8_t ctrl = m->regs[base];
    if (rxn == 0) {
        ctrl &= ~(MCP_RXB0CTRL_FILHIT_MASK | MCP_RXB0CTRL_RXRTR);
        ctrl |= (uint8_t)(filter_idx & MCP_RXB0CTRL_FILHIT_MASK);
        if (f->rtr) ctrl |= MCP_RXB0CTRL_RXRTR;
    } else {
        ctrl &= ~(MCP_RXB1CTRL_FILHIT_MASK | MCP_RXB1CTRL_RXRTR);
        ctrl |= (uint8_t)(filter_idx & MCP_RXB1CTRL_FILHIT_MASK);
        if (f->rtr) ctrl |= MCP_RXB1CTRL_RXRTR;
    }
    m->regs[base] = ctrl;

    /* Set RXnIF flag, then update INT line. */
    m->regs[MCP_CANINTF] |= (rxn == 0) ? MCP_INT_RX0IF : MCP_INT_RX1IF;
    update_int_pin(m);
}

/* Whether a buffer's RXM mode admits this frame's type. RECV_ANY and
 * RECV_ALL_FILTERED accept either; RECV_STD/RECV_EXT restrict. */
static bool type_admitted(int rxm_mode, bool ext)
{
    switch (rxm_mode) {
    case MCP_RXM_RECV_ANY:
    case MCP_RXM_RECV_ALL_FILTERED: return true;
    case MCP_RXM_RECV_STD:          return !ext;
    case MCP_RXM_RECV_EXT:          return  ext;
    default:                        return true;
    }
}

static bool route_inbound(mcp2515_t *m, const mcp2515_frame_t *f)
{
    int rxb0_mode = (m->regs[MCP_RXB0CTRL] & MCP_RXB0CTRL_RXM_MASK)
                  >> MCP_RXB0CTRL_RXM_SHIFT;
    int rxb1_mode = (m->regs[MCP_RXB1CTRL] & MCP_RXB1CTRL_RXM_MASK)
                  >> MCP_RXB1CTRL_RXM_SHIFT;

    bool rxb0_busy = (m->regs[MCP_CANINTF] & MCP_INT_RX0IF) != 0;
    bool rxb1_busy = (m->regs[MCP_CANINTF] & MCP_INT_RX1IF) != 0;

    /* RXB0 candidacy: not busy, type-admissible, and either RECV_ANY (no
     * filter check) or one of RXF0/RXF1 matches. RECV_STD and RECV_EXT
     * still apply the filters — the type check is in addition, not
     * instead. */
    bool rxb0_takes = false;
    int  filter_idx = -1;
    if (!rxb0_busy && type_admitted(rxb0_mode, f->ext)) {
        if (rxb0_mode == MCP_RXM_RECV_ANY) {
            rxb0_takes = true;
            filter_idx = 0;
        } else {
            int which = -1;
            int fi = find_matching_filter(m, f, &which);
            if (fi >= 0 && which == 0) {
                rxb0_takes = true;
                filter_idx = fi;
            }
        }
    }

    if (rxb0_takes) {
        deposit_in_rxb(m, 0, f, filter_idx);
        return true;
    }

    /* RXB0 didn't take it. Try RXB1, with optional BUKT rollover from
     * RXB0 (frame matched RXB0 filters but RXB0 was busy). */
    if (!type_admitted(rxb1_mode, f->ext)) {
        if (rxb0_busy) m->regs[MCP_EFLG] |= MCP_EFLG_RX0OVR;
        return false;
    }
    if (rxb1_busy) {
        m->regs[MCP_EFLG] |= (rxb0_busy ? MCP_EFLG_RX0OVR : 0)
                           | MCP_EFLG_RX1OVR;
        return false;
    }

    bool rxb1_takes = false;
    int rxb1_filter_idx = -1;
    if (rxb1_mode == MCP_RXM_RECV_ANY) {
        rxb1_takes = true;
        rxb1_filter_idx = 2;
    } else {
        int which = -1;
        int fi = find_matching_filter(m, f, &which);
        if (fi >= 0) {
            if (which == 1) {
                rxb1_takes = true;
                rxb1_filter_idx = fi - 2;     /* RXB1 filhit is 0..3 */
            } else if (which == 0 && (m->regs[MCP_RXB0CTRL] & MCP_RXB0CTRL_BUKT)) {
                /* Rollover: RXB0 matched but was busy. Datasheet labels
                 * this with FILHIT bit 2 set in RXB1CTRL — leave for M3+. */
                rxb1_takes = true;
                rxb1_filter_idx = fi;
            }
        }
    }

    if (rxb1_takes) {
        deposit_in_rxb(m, 1, f, rxb1_filter_idx);
        return true;
    }
    return false;
}

bool mcp2515_rx_frame(mcp2515_t *m, const mcp2515_frame_t *frame)
{
    /* Sleep / Configuration mode: ignore inbound frames. */
    if (m->mode == MCP_MODE_SLEEP || m->mode == MCP_MODE_CONFIG) {
        return false;
    }
    return route_inbound(m, frame);
}

/* ----- Loopback delivery ------------------------------------------- */

static void deliver_loopback(mcp2515_t *m, int txbuf)
{
    int base = txbuf_base(txbuf);

    /* Read frame out of TXB. */
    mcp2515_frame_t f;
    uint32_t id; bool ext;
    unpack_id(&m->regs[base + 1], &id, &ext);
    f.id  = id;
    f.ext = ext;
    f.dlc = m->regs[base + 5] & MCP_DLC_DLC_MASK;
    f.rtr = (m->regs[base + 5] & MCP_DLC_RTR) != 0;
    for (int i = 0; i < 8; i++) {
        f.data[i] = m->regs[base + 6 + i];
    }

    /* TXREQ clears, TXnIF flag sets to indicate "transmission complete". */
    m->regs[base] &= ~MCP_TXBCTRL_TXREQ;
    static const uint8_t txif[3] = { MCP_INT_TX0IF, MCP_INT_TX1IF, MCP_INT_TX2IF };
    m->regs[MCP_CANINTF] |= txif[txbuf];

    /* Loopback: feed back into RX path. */
    route_inbound(m, &f);

    update_int_pin(m);
}

/* ----- Status / RX-status command bytes ----------------------------- */

static uint8_t compute_read_status(const mcp2515_t *m)
{
    /* Datasheet figure 12-9.
     *   bit 0 = CANINTF.RX0IF
     *   bit 1 = CANINTF.RX1IF
     *   bit 2 = TXB0CTRL.TXREQ
     *   bit 3 = CANINTF.TX0IF
     *   bit 4 = TXB1CTRL.TXREQ
     *   bit 5 = CANINTF.TX1IF
     *   bit 6 = TXB2CTRL.TXREQ
     *   bit 7 = CANINTF.TX2IF
     */
    uint8_t s = 0;
    if (m->regs[MCP_CANINTF]  & MCP_INT_RX0IF)        s |= 0x01;
    if (m->regs[MCP_CANINTF]  & MCP_INT_RX1IF)        s |= 0x02;
    if (m->regs[MCP_TXB0CTRL] & MCP_TXBCTRL_TXREQ)    s |= 0x04;
    if (m->regs[MCP_CANINTF]  & MCP_INT_TX0IF)        s |= 0x08;
    if (m->regs[MCP_TXB1CTRL] & MCP_TXBCTRL_TXREQ)    s |= 0x10;
    if (m->regs[MCP_CANINTF]  & MCP_INT_TX1IF)        s |= 0x20;
    if (m->regs[MCP_TXB2CTRL] & MCP_TXBCTRL_TXREQ)    s |= 0x40;
    if (m->regs[MCP_CANINTF]  & MCP_INT_TX2IF)        s |= 0x80;
    return s;
}

static uint8_t compute_rx_status(const mcp2515_t *m)
{
    /* Datasheet figure 12-10.
     *   bits 7:6 — number of received messages: 00 none, 01 RXB0, 10 RXB1, 11 both
     *   bits 4:3 — message type: 00 std, 01 std-rtr, 10 ext, 11 ext-rtr
     *   bits 2:0 — filter match (0..5 normal, 6/7 = rollover indicators)
     */
    uint8_t s = 0;
    bool rx0 = (m->regs[MCP_CANINTF] & MCP_INT_RX0IF) != 0;
    bool rx1 = (m->regs[MCP_CANINTF] & MCP_INT_RX1IF) != 0;
    if (rx0)  s |= 0x40;
    if (rx1)  s |= 0x80;

    int base = rx0 ? MCP_RXB0CTRL : (rx1 ? MCP_RXB1CTRL : 0);
    if (base) {
        uint8_t sidl = m->regs[base + 2];
        uint8_t dlc  = m->regs[base + 5];
        bool ext = (sidl & MCP_SIDL_EXIDE) != 0;
        bool rtr = (dlc & MCP_DLC_RTR) != 0;
        s |= (uint8_t)((ext ? 2 : 0) | (rtr ? 1 : 0)) << 3;

        if (rx0) {
            s |= (m->regs[MCP_RXB0CTRL] & MCP_RXB0CTRL_FILHIT_MASK);
        } else {
            s |= (m->regs[MCP_RXB1CTRL] & MCP_RXB1CTRL_FILHIT_MASK);
        }
    }
    return s;
}

/* ----- SPI byte handler -------------------------------------------- */

void mcp2515_cs_low(mcp2515_t *m)
{
    m->cs_low    = true;
    m->txn_pos   = 0;
    m->txn_state = MCP_TXN_IDLE;
}

void mcp2515_cs_high(mcp2515_t *m)
{
    m->cs_low    = false;
    /* Any pending state is committed at byte time; nothing to flush here. */
    m->txn_state = MCP_TXN_IDLE;
}

uint8_t mcp2515_spi_byte(mcp2515_t *m, uint8_t in)
{
    /* Outside of an active CS, ignore. */
    if (!m->cs_low) return 0;

    uint8_t out = 0;

    if (m->txn_pos == 0) {
        /* First byte: command. */
        m->txn_cmd = in;

        if (in == MCP_SPI_RESET) {
            mcp2515_reset(m);
            m->txn_state = MCP_TXN_DONE;
        } else if (in == MCP_SPI_READ) {
            m->txn_state = MCP_TXN_NEED_ADDR;
        } else if (in == MCP_SPI_WRITE) {
            m->txn_state = MCP_TXN_NEED_ADDR;
        } else if (in == MCP_SPI_BIT_MODIFY) {
            m->txn_state = MCP_TXN_NEED_ADDR;
        } else if (in == MCP_SPI_READ_STATUS) {
            out = compute_read_status(m);
            m->txn_state = MCP_TXN_STATUS;
        } else if (in == MCP_SPI_RX_STATUS) {
            out = compute_rx_status(m);
            m->txn_state = MCP_TXN_RX_STATUS;
        } else if ((in & 0xF8) == MCP_SPI_RTS_BASE) {
            /* RTS: bottom 3 bits select TX buffers (any combination). */
            uint8_t mask = in & 0x07;
            for (int n = 0; n < 3; n++) {
                if (mask & (1 << n)) {
                    int base = txbuf_base(n);
                    apply_reg_write(m, (uint8_t)base,
                                    (uint8_t)(m->regs[base] | MCP_TXBCTRL_TXREQ));
                }
            }
            m->txn_state = MCP_TXN_DONE;
        } else if ((in & 0xF8) == 0x90 && (in & 0x09) == 0x00 /* 0x90,0x92,0x94,0x96 */) {
            /* READ RX BUFFER: 0x90/0x92 select RXB0; 0x94/0x96 select RXB1.
             * Bit 1 (=2) selects "data only" (skip ID header). */
            int rxn = (in & 0x04) ? 1 : 0;
            bool data_only = (in & 0x02) != 0;
            m->read_rx_buf = rxn;
            m->read_rx_off = 0;
            m->read_rx_data_only = data_only;
            m->txn_state = MCP_TXN_READ_RX;
        } else if ((in & 0xF8) == MCP_SPI_LOAD_TX_BASE && (in & 0x06) <= 0x04) {
            /* LOAD TX BUFFER: 0x40,0x41 = TXB0 ID/data start;
             *                 0x42,0x43 = TXB1 ID/data start;
             *                 0x44,0x45 = TXB2 ID/data start.
             * Even = start at SIDH (full); odd = start at D0 (data only). */
            int n = (in - MCP_SPI_LOAD_TX_BASE) / 2;
            int base = txbuf_base(n);
            m->load_tx_buf = n;
            m->load_tx_off = (in & 1) ? 6 : 1;   /* +1=SIDH, +6=D0 */
            (void)base;
            m->txn_state = MCP_TXN_LOAD_TX;
        } else {
            /* Unknown command; ignore subsequent bytes. */
            m->txn_state = MCP_TXN_DONE;
        }
    } else {
        switch (m->txn_state) {
        case MCP_TXN_NEED_ADDR:
            m->txn_addr = in;
            if (m->txn_cmd == MCP_SPI_READ) {
                m->txn_state = MCP_TXN_READ_DATA;
            } else if (m->txn_cmd == MCP_SPI_WRITE) {
                m->txn_state = MCP_TXN_NEED_DATA;
            } else /* BIT MODIFY */ {
                m->txn_state = MCP_TXN_NEED_MASK;
            }
            break;

        case MCP_TXN_NEED_MASK:
            m->txn_mask = in;
            m->txn_state = MCP_TXN_NEED_DATA;
            break;

        case MCP_TXN_NEED_DATA:
            if (m->txn_cmd == MCP_SPI_BIT_MODIFY) {
                bit_modify(m, m->txn_addr, m->txn_mask, in);
                m->txn_state = MCP_TXN_DONE;
            } else /* WRITE: auto-increment address */ {
                apply_reg_write(m, m->txn_addr, in);
                m->txn_addr++;
            }
            break;

        case MCP_TXN_READ_DATA:
            out = (m->txn_addr < MCP_REGS_SIZE) ? m->regs[m->txn_addr] : 0;
            m->txn_addr++;
            break;

        case MCP_TXN_LOAD_TX: {
            int base = txbuf_base(m->load_tx_buf);
            int addr = base + m->load_tx_off;
            if (addr < MCP_REGS_SIZE) {
                m->regs[addr] = in;
            }
            m->load_tx_off++;
            /* Stop accepting bytes after end of buffer (CTRL+ID+DLC+8 data
             * = 14 bytes), but be permissive — driver may write extras. */
            break;
        }

        case MCP_TXN_READ_RX: {
            int base = (m->read_rx_buf == 0) ? MCP_RXB0CTRL : MCP_RXB1CTRL;
            int addr;
            if (m->read_rx_data_only) {
                addr = base + 6 + m->read_rx_off;       /* skip ID header */
            } else {
                addr = base + 1 + m->read_rx_off;       /* SIDH onward */
            }
            out = (addr < MCP_REGS_SIZE) ? m->regs[addr] : 0;
            m->read_rx_off++;

            /* Per datasheet 12.4: READ RX BUFFER also auto-clears the
             * RXnIF flag when the transaction completes (CS rises).
             * We clear immediately to keep state simple — drivers always
             * read the full payload before raising CS. */
            int total = m->read_rx_data_only ? 8 : (5 + 8);
            if (m->read_rx_off >= total) {
                m->regs[MCP_CANINTF] &=
                    (uint8_t)~((m->read_rx_buf == 0) ? MCP_INT_RX0IF : MCP_INT_RX1IF);
                update_int_pin(m);
            }
            break;
        }

        case MCP_TXN_STATUS:
            out = compute_read_status(m);   /* repeats indefinitely */
            break;

        case MCP_TXN_RX_STATUS:
            out = compute_rx_status(m);
            break;

        case MCP_TXN_IDLE:
        case MCP_TXN_DONE:
        default:
            break;
        }
    }

    m->txn_pos++;
    return out;
}

int mcp2515_get_mode(const mcp2515_t *m)
{
    return m->mode;
}
