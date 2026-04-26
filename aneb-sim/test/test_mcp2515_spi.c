/*
 * test_mcp2515_spi.c — SPI command-set behaviors that aren't bare register I/O:
 * READ STATUS / RX STATUS, RTS, LOAD TX BUFFER, READ RX BUFFER auto-clear.
 */
#include "test.h"
#include "test_helpers.h"
#include "mcp2515.h"
#include "mcp2515_regs.h"

int test_read_status_idle(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    TEST_ASSERT_EQ_HEX(spi_read_status(&m), 0,
                       "no flags set, status all-zero");
    return 0;
}

int test_rx_status_idle(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    TEST_ASSERT_EQ_HEX(spi_rx_status(&m), 0, "no rx, rx-status zero");
    return 0;
}

int test_rts_sets_txreq(void)
{
    /* Verify the register-level SIDE EFFECTS of RTS — that the bottom 3
     * bits of the command byte select which TXBnCTRL.TXREQ bits get set.
     * Stay in Configuration mode (the reset default) so the request
     * stays pending — in NORMAL mode, deliver_normal would immediately
     * clear TXREQ as the transmission completes (covered by separate
     * tests in the can_bus suite). */
    mcp2515_t m; mcp2515_init(&m, "t");

    /* RTS for TXB1 only. */
    spi_rts(&m, 0x02);
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_TXB1CTRL) & MCP_TXBCTRL_TXREQ,
                "TXB1.TXREQ should be set after RTS(0x02)");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_TXB0CTRL) & MCP_TXBCTRL_TXREQ),
                "TXB0.TXREQ should NOT be set");

    /* RTS for TXB0 + TXB2 simultaneously. */
    spi_rts(&m, 0x05);
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_TXB0CTRL) & MCP_TXBCTRL_TXREQ,
                "TXB0.TXREQ set");
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_TXB2CTRL) & MCP_TXBCTRL_TXREQ,
                "TXB2.TXREQ set");

    /* READ STATUS reflects all three pending TXREQs at bits 2/4/6. */
    uint8_t s = spi_read_status(&m);
    TEST_ASSERT(s & 0x04, "READ STATUS bit 2 = TXB0.TXREQ");
    TEST_ASSERT(s & 0x10, "READ STATUS bit 4 = TXB1.TXREQ");
    TEST_ASSERT(s & 0x40, "READ STATUS bit 6 = TXB2.TXREQ");
    return 0;
}

int test_load_tx_full(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    /* LOAD TX BUFFER 0 starting at SIDH (0x40): write SIDH..D7. */
    mcp2515_cs_low(&m);
    mcp2515_spi_byte(&m, 0x40);
    mcp2515_spi_byte(&m, 0xAA); /* SIDH */
    mcp2515_spi_byte(&m, 0xBB); /* SIDL */
    mcp2515_spi_byte(&m, 0xCC); /* EID8 */
    mcp2515_spi_byte(&m, 0xDD); /* EID0 */
    mcp2515_spi_byte(&m, 0x08); /* DLC */
    for (int i = 0; i < 8; i++) {
        mcp2515_spi_byte(&m, (uint8_t)(0x10 + i));
    }
    mcp2515_cs_high(&m);

    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_TXB0SIDH), 0xAA, "SIDH");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_TXB0DLC),  0x08, "DLC");
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, (uint8_t)(MCP_TXB0D0 + i)),
                           0x10 + i, "data byte");
    }
    return 0;
}

int test_load_tx_data_only(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    /* Pre-set ID/DLC, then use 0x41 (LOAD TX 0, data-only) to write data. */
    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0x12);
    mcp2515_reg_write(&m, MCP_TXB0DLC,  0x04);

    mcp2515_cs_low(&m);
    mcp2515_spi_byte(&m, 0x41);
    for (int i = 0; i < 4; i++) {
        mcp2515_spi_byte(&m, (uint8_t)(0xA0 + i));
    }
    mcp2515_cs_high(&m);

    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_TXB0SIDH), 0x12, "ID untouched");
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, (uint8_t)(MCP_TXB0D0 + i)),
                           0xA0 + i, "data byte");
    }
    return 0;
}

int test_read_rx_clears_flag(void)
{
    /* Stage an RX0 buffer manually, set RX0IF, then perform a READ RX BUFFER
     * 0x90 (RXB0 starting at SIDH) and observe that RX0IF clears once the
     * full buffer has been read. */
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    /* Pretend a frame was received: write fake RX0 data + raise RX0IF. */
    mcp2515_reg_write(&m, MCP_RXB0SIDH, 0x77);
    mcp2515_reg_write(&m, MCP_RXB0DLC,  0x02);
    mcp2515_reg_write(&m, MCP_CANINTF,  MCP_INT_RX0IF);

    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_RX0IF,
                "RX0IF set before read");

    /* Read 5 (header) + 8 (data) = 13 bytes. */
    mcp2515_cs_low(&m);
    mcp2515_spi_byte(&m, 0x90);
    for (int i = 0; i < 13; i++) {
        (void)mcp2515_spi_byte(&m, 0);
    }
    mcp2515_cs_high(&m);

    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_RX0IF),
                "RX0IF cleared after full buffer read");
    return 0;
}
