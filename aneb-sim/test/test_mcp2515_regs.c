/*
 * test_mcp2515_regs.c — register-file semantics and SPI READ/WRITE/BIT
 * MODIFY commands, mode transitions via CANCTRL, and the SPI RESET.
 */
#include "test.h"
#include "test_helpers.h"
#include "mcp2515.h"
#include "mcp2515_regs.h"

int test_reset_values(void)
{
    mcp2515_t m;
    mcp2515_init(&m, "test");
    TEST_ASSERT_EQ(mcp2515_get_mode(&m), MCP_MODE_CONFIG, "reset enters Config");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANCTRL), 0x87, "CANCTRL reset 0x87");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANSTAT) & MCP_CANSTAT_OPMOD_MASK,
                       (uint8_t)MCP_MODE_CONFIG << MCP_CANSTAT_OPMOD_SHIFT,
                       "CANSTAT.OPMOD = Config");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANINTF), 0, "CANINTF clear at reset");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_TEC), 0, "TEC clear");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_REC), 0, "REC clear");
    return 0;
}

int test_write_then_read(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    spi_write_one(&m, MCP_CNF1, 0xA5);
    TEST_ASSERT_EQ_HEX(spi_read_one(&m, MCP_CNF1), 0xA5, "CNF1 round-trip");
    spi_write_one(&m, MCP_RXM0SIDH, 0x12);
    TEST_ASSERT_EQ_HEX(spi_read_one(&m, MCP_RXM0SIDH), 0x12, "RXM0SIDH round-trip");
    return 0;
}

int test_write_auto_increment(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    /* Multi-byte WRITE: addr autoincrement across SIDH/SIDL/EID8/EID0. */
    mcp2515_cs_low(&m);
    mcp2515_spi_byte(&m, MCP_SPI_WRITE);
    mcp2515_spi_byte(&m, MCP_RXF0SIDH);
    mcp2515_spi_byte(&m, 0x11);
    mcp2515_spi_byte(&m, 0x22);
    mcp2515_spi_byte(&m, 0x33);
    mcp2515_spi_byte(&m, 0x44);
    mcp2515_cs_high(&m);

    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXF0SIDH), 0x11, "SIDH");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXF0SIDL), 0x22, "SIDL");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXF0EID8), 0x33, "EID8");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_RXF0EID0), 0x44, "EID0");
    return 0;
}

int test_read_auto_increment(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    /* Pre-load four registers via direct API. */
    mcp2515_reg_write(&m, MCP_RXF0SIDH, 0x55);
    mcp2515_reg_write(&m, MCP_RXF0SIDL, 0x66);
    mcp2515_reg_write(&m, MCP_RXF0EID8, 0x77);
    mcp2515_reg_write(&m, MCP_RXF0EID0, 0x88);

    /* Multi-byte READ: addr autoincrement. */
    mcp2515_cs_low(&m);
    mcp2515_spi_byte(&m, MCP_SPI_READ);
    mcp2515_spi_byte(&m, MCP_RXF0SIDH);
    uint8_t a = mcp2515_spi_byte(&m, 0);
    uint8_t b = mcp2515_spi_byte(&m, 0);
    uint8_t c = mcp2515_spi_byte(&m, 0);
    uint8_t d = mcp2515_spi_byte(&m, 0);
    mcp2515_cs_high(&m);

    TEST_ASSERT_EQ_HEX(a, 0x55, "byte 0");
    TEST_ASSERT_EQ_HEX(b, 0x66, "byte 1");
    TEST_ASSERT_EQ_HEX(c, 0x77, "byte 2");
    TEST_ASSERT_EQ_HEX(d, 0x88, "byte 3");
    return 0;
}

int test_bit_modify(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_reg_write(&m, MCP_CANINTE, 0x00);

    /* Set RX0IF and TX0IF only. */
    spi_bit_modify(&m, MCP_CANINTE,
                   MCP_INT_RX0IF | MCP_INT_TX0IF,
                   MCP_INT_RX0IF | MCP_INT_TX0IF);
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANINTE),
                       MCP_INT_RX0IF | MCP_INT_TX0IF, "set masked bits");

    /* Clear only TX0IF, leaving RX0IF intact. */
    spi_bit_modify(&m, MCP_CANINTE, MCP_INT_TX0IF, 0x00);
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANINTE),
                       MCP_INT_RX0IF, "clear only one masked bit");

    /* Bits outside mask are preserved when value carries them. */
    spi_bit_modify(&m, MCP_CANINTE, MCP_INT_RX1IF, 0xFF);
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANINTE),
                       MCP_INT_RX0IF | MCP_INT_RX1IF, "value bits outside mask ignored");
    return 0;
}

int test_canstat_readonly(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    uint8_t before = mcp2515_reg_read(&m, MCP_CANSTAT);
    spi_write_one(&m, MCP_CANSTAT, 0x00);
    uint8_t after = mcp2515_reg_read(&m, MCP_CANSTAT);
    TEST_ASSERT_EQ_HEX(after, before, "CANSTAT ignores direct writes");
    return 0;
}

int test_mode_transition_via_canctrl(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");

    /* Config -> Loopback */
    spi_write_one(&m, MCP_CANCTRL,
                  (uint8_t)MCP_MODE_LOOPBACK << MCP_CANCTRL_REQOP_SHIFT);
    TEST_ASSERT_EQ(mcp2515_get_mode(&m), MCP_MODE_LOOPBACK, "Config -> Loopback");
    TEST_ASSERT_EQ_HEX(
        (mcp2515_reg_read(&m, MCP_CANSTAT) & MCP_CANSTAT_OPMOD_MASK)
            >> MCP_CANSTAT_OPMOD_SHIFT,
        MCP_MODE_LOOPBACK, "CANSTAT.OPMOD reflects Loopback");

    /* Loopback -> Normal */
    spi_write_one(&m, MCP_CANCTRL,
                  (uint8_t)MCP_MODE_NORMAL << MCP_CANCTRL_REQOP_SHIFT);
    TEST_ASSERT_EQ(mcp2515_get_mode(&m), MCP_MODE_NORMAL, "Loopback -> Normal");

    /* Normal -> Config */
    spi_write_one(&m, MCP_CANCTRL,
                  (uint8_t)MCP_MODE_CONFIG << MCP_CANCTRL_REQOP_SHIFT);
    TEST_ASSERT_EQ(mcp2515_get_mode(&m), MCP_MODE_CONFIG, "Normal -> Config");
    return 0;
}

int test_reset_via_spi_command(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");

    /* Dirty several registers, then issue SPI RESET. */
    mcp2515_reg_write(&m, MCP_CNF1, 0xFF);
    mcp2515_reg_write(&m, MCP_CANINTE, 0xFF);
    set_mode_normal(&m);

    spi_reset(&m);

    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CNF1), 0,    "CNF1 cleared");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANINTE), 0, "CANINTE cleared");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_CANCTRL), 0x87, "CANCTRL reset");
    TEST_ASSERT_EQ(mcp2515_get_mode(&m), MCP_MODE_CONFIG, "back to Config");
    return 0;
}
