/*
 * test_mcp2515_loopback.c — exit criterion for M2.
 *
 * In Loopback mode, a frame written into TXB0 + RTS should appear in
 * RXB0 (subject to filtering), with RX0IF and TX0IF both set, and the
 * INT pin asserted.
 */
#include "test.h"
#include "test_helpers.h"
#include "mcp2515.h"
#include "mcp2515_regs.h"

typedef struct {
    int int_value;
    int int_changes;
} cb_state_t;

static void on_int(void *ctx, int asserted)
{
    cb_state_t *s = (cb_state_t *)ctx;
    s->int_value = asserted;
    s->int_changes++;
}

static void program_pass_all(mcp2515_t *m)
{
    /* Mask 0 / Mask 1 = 0 → don't care. Filter 0 + Filter 2 set to 0 to
     * receive any std frame in either RXB0 or RXB1. */
    for (int i = 0; i < 4; i++) {
        mcp2515_reg_write(m, MCP_RXM0SIDH + i, 0);
        mcp2515_reg_write(m, MCP_RXM1SIDH + i, 0);
    }
}

int test_loopback_round_trip(void)
{
    cb_state_t cb = {0};
    mcp2515_t  m;
    mcp2515_init(&m, "t");
    m.on_int = on_int;
    m.ctx    = &cb;
    program_pass_all(&m);

    set_mode_loopback(&m);

    /* Stage frame in TXB0: ID=0x123, DLC=2, data=0xCAFE. */
    /* SIDH = 0x123 >> 3 = 0x24, SIDL = (0x123 & 7) << 5 = 0x60 */
    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0x24);
    mcp2515_reg_write(&m, MCP_TXB0SIDL, 0x60);
    mcp2515_reg_write(&m, MCP_TXB0DLC,  0x02);
    mcp2515_reg_write(&m, MCP_TXB0D0,   0xCA);
    mcp2515_reg_write(&m, MCP_TXB0D0+1, 0xFE);

    /* Trigger transmission. */
    spi_rts(&m, 0x01);

    /* Frame should now be in RXB0. */
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXB0SIDH), 0x24, "SIDH echoed");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXB0SIDL) & 0xE0, 0x60, "SIDL.SID2:0");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXB0DLC) & 0x0F, 2, "DLC");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXB0D0),   0xCA, "data[0]");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXB0D0+1), 0xFE, "data[1]");
    return 0;
}

int test_loopback_int_pin_asserted(void)
{
    cb_state_t cb = {0};
    mcp2515_t  m;
    mcp2515_init(&m, "t");
    m.on_int = on_int;
    m.ctx    = &cb;
    program_pass_all(&m);
    set_mode_loopback(&m);

    /* Enable RX0IF and TX0IF interrupts. */
    mcp2515_reg_write(&m, MCP_CANINTE, MCP_INT_RX0IF | MCP_INT_TX0IF);

    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0);
    mcp2515_reg_write(&m, MCP_TXB0DLC,  0);
    spi_rts(&m, 0x01);

    TEST_ASSERT_EQ(cb.int_value, 1, "INT pin asserted after loopback");
    TEST_ASSERT(cb.int_changes >= 1, "at least one INT edge observed");

    /* Clear flags via BIT MODIFY → INT pin should release. */
    spi_bit_modify(&m, MCP_CANINTF,
                   MCP_INT_RX0IF | MCP_INT_TX0IF, 0);
    TEST_ASSERT_EQ(cb.int_value, 0, "INT pin released after flags cleared");
    return 0;
}

int test_loopback_txif_set(void)
{
    mcp2515_t m;
    mcp2515_init(&m, "t");
    program_pass_all(&m);
    set_mode_loopback(&m);

    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0);
    spi_rts(&m, 0x01);

    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_TX0IF,
                "TX0IF set after loopback transmit");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_TXB0CTRL) & MCP_TXBCTRL_TXREQ),
                "TXREQ cleared after loopback completes");
    return 0;
}

int test_loopback_filtered_out(void)
{
    /* When filters are configured to NOT match the TX'd frame, the loopback
     * still completes (TX0IF set, TXREQ cleared) but no RXnIF fires. */
    mcp2515_t m;
    mcp2515_init(&m, "t");

    /* Mask exact match on RXB0; filter 0 = 0x100; mask everything on RXB1
     * (so accidental match is impossible). */
    mcp2515_reg_write(&m, MCP_RXM0SIDH, 0xFF);
    mcp2515_reg_write(&m, MCP_RXM0SIDL, 0xE0);
    mcp2515_reg_write(&m, MCP_RXM1SIDH, 0xFF);
    mcp2515_reg_write(&m, MCP_RXM1SIDL, 0xE0);
    mcp2515_reg_write(&m, MCP_RXF0SIDH, 0x20);  /* 0x100 >> 3 = 0x20 */
    mcp2515_reg_write(&m, MCP_RXF0SIDL, 0x00);
    mcp2515_reg_write(&m, MCP_RXF1SIDH, 0x20);
    mcp2515_reg_write(&m, MCP_RXF2SIDH, 0x20);
    mcp2515_reg_write(&m, MCP_RXF3SIDH, 0x20);
    mcp2515_reg_write(&m, MCP_RXF4SIDH, 0x20);
    mcp2515_reg_write(&m, MCP_RXF5SIDH, 0x20);

    set_mode_loopback(&m);

    /* TX a frame with id 0x200 — should not match any filter. */
    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0x40);  /* 0x200 >> 3 */
    mcp2515_reg_write(&m, MCP_TXB0SIDL, 0x00);
    mcp2515_reg_write(&m, MCP_TXB0DLC,  0x00);
    spi_rts(&m, 0x01);

    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_TX0IF,
                "TX0IF still set");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_RX0IF),
                "RX0IF NOT set (frame filtered out)");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_RX1IF),
                "RX1IF NOT set");
    return 0;
}
