/*
 * test_helpers.h — shared bytecode-level shortcuts for driving the
 * MCP2515 model from tests. All helpers wrap CS-low / SPI bytes / CS-high
 * sequences so individual tests remain readable.
 */
#ifndef ANEB_TEST_HELPERS_H
#define ANEB_TEST_HELPERS_H

#include <stdint.h>
#include "mcp2515.h"

static inline void spi_reset(mcp2515_t *m)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, MCP_SPI_RESET);
    mcp2515_cs_high(m);
}

static inline void spi_write_one(mcp2515_t *m, uint8_t addr, uint8_t val)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, MCP_SPI_WRITE);
    mcp2515_spi_byte(m, addr);
    mcp2515_spi_byte(m, val);
    mcp2515_cs_high(m);
}

static inline uint8_t spi_read_one(mcp2515_t *m, uint8_t addr)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, MCP_SPI_READ);
    mcp2515_spi_byte(m, addr);
    uint8_t v = mcp2515_spi_byte(m, 0x00);
    mcp2515_cs_high(m);
    return v;
}

static inline void spi_bit_modify(mcp2515_t *m, uint8_t addr,
                                  uint8_t mask, uint8_t val)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, MCP_SPI_BIT_MODIFY);
    mcp2515_spi_byte(m, addr);
    mcp2515_spi_byte(m, mask);
    mcp2515_spi_byte(m, val);
    mcp2515_cs_high(m);
}

static inline uint8_t spi_read_status(mcp2515_t *m)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, MCP_SPI_READ_STATUS);
    uint8_t v = mcp2515_spi_byte(m, 0x00);
    mcp2515_cs_high(m);
    return v;
}

static inline uint8_t spi_rx_status(mcp2515_t *m)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, MCP_SPI_RX_STATUS);
    uint8_t v = mcp2515_spi_byte(m, 0x00);
    mcp2515_cs_high(m);
    return v;
}

static inline void spi_rts(mcp2515_t *m, uint8_t mask)
{
    mcp2515_cs_low(m);
    mcp2515_spi_byte(m, (uint8_t)(MCP_SPI_RTS_BASE | (mask & 0x07)));
    mcp2515_cs_high(m);
}

static inline void set_mode_normal(mcp2515_t *m)
{
    spi_write_one(m, MCP_CANCTRL,
                  (uint8_t)MCP_MODE_NORMAL << MCP_CANCTRL_REQOP_SHIFT);
}

static inline void set_mode_loopback(mcp2515_t *m)
{
    spi_write_one(m, MCP_CANCTRL,
                  (uint8_t)MCP_MODE_LOOPBACK << MCP_CANCTRL_REQOP_SHIFT);
}

#endif
