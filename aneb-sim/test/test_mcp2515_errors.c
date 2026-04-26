/*
 * test_mcp2515_errors.c — TEC/REC counters, EFLG thresholds, error-active
 * → error-passive → bus-off transitions, bus-off recovery, and TX/RX
 * gating in bus-off.
 */
#include "test.h"
#include "test_helpers.h"
#include "mcp2515.h"
#include "mcp2515_regs.h"

static void open_filters(mcp2515_t *m)
{
    for (int i = 0; i < 4; i++) {
        mcp2515_reg_write(m, MCP_RXM0SIDH + i, 0);
        mcp2515_reg_write(m, MCP_RXM1SIDH + i, 0);
    }
}

int test_initial_state_active(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    TEST_ASSERT_EQ(mcp2515_err_state(&m), MCP_ERR_STATE_ACTIVE,
                   "fresh controller is error-active");
    TEST_ASSERT_EQ_HEX(mcp2515_tec(&m), 0, "TEC=0");
    TEST_ASSERT_EQ_HEX(mcp2515_rec(&m), 0, "REC=0");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_EFLG), 0, "EFLG clean");
    return 0;
}

int test_inject_tx_error_increments_tec(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_inject_tx_errors(&m, 1);   /* +8 */
    TEST_ASSERT_EQ(mcp2515_tec(&m), 8, "TEC += 8 per inject count");
    mcp2515_inject_tx_errors(&m, 4);   /* +32 */
    TEST_ASSERT_EQ(mcp2515_tec(&m), 40, "cumulative TEC");
    /* ERRIF + MERRF should be raised. */
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_ERRIF, "ERRIF set");
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_MERRF, "MERRF set");
    return 0;
}

int test_inject_rx_error_increments_rec(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_inject_rx_errors(&m, 5);
    TEST_ASSERT_EQ(mcp2515_rec(&m), 5, "REC += 1 per inject count");
    /* ERRIF only — RX errors don't set MERRF. */
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_ERRIF, "ERRIF set");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_MERRF),
                "MERRF should NOT be set on rx-only");
    return 0;
}

int test_warning_threshold_eflg(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    /* TEC = 96 -> EWARN + TXWAR */
    mcp2515_inject_tx_errors(&m, 12);   /* 12 * 8 = 96 */
    TEST_ASSERT_EQ(mcp2515_tec(&m), 96, "TEC at warning threshold");
    uint8_t e = mcp2515_reg_read(&m, MCP_EFLG);
    TEST_ASSERT(e & MCP_EFLG_EWARN, "EWARN set at TEC=96");
    TEST_ASSERT(e & MCP_EFLG_TXWAR, "TXWAR set at TEC=96");
    TEST_ASSERT(!(e & MCP_EFLG_TXEP), "TXEP NOT set yet");
    TEST_ASSERT(!(e & MCP_EFLG_TXBO), "TXBO NOT set yet");
    return 0;
}

int test_passive_threshold_via_tec(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_inject_tx_errors(&m, 16);   /* 16 * 8 = 128 */
    TEST_ASSERT_EQ(mcp2515_tec(&m), 128, "TEC at passive threshold");
    TEST_ASSERT_EQ(mcp2515_err_state(&m), MCP_ERR_STATE_PASSIVE,
                   "state == passive");
    uint8_t e = mcp2515_reg_read(&m, MCP_EFLG);
    TEST_ASSERT(e & MCP_EFLG_TXEP, "TXEP set");
    TEST_ASSERT(!(e & MCP_EFLG_RXEP), "RXEP NOT set (REC still low)");
    TEST_ASSERT(!(e & MCP_EFLG_TXBO), "TXBO NOT set");
    return 0;
}

int test_passive_threshold_via_rec(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_inject_rx_errors(&m, 128);
    TEST_ASSERT_EQ(mcp2515_err_state(&m), MCP_ERR_STATE_PASSIVE, "passive via REC");
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_EFLG) & MCP_EFLG_RXEP, "RXEP set");
    return 0;
}

int test_busoff_threshold(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    /* Saturate TEC. */
    mcp2515_inject_tx_errors(&m, 100);
    TEST_ASSERT_EQ(mcp2515_tec(&m), 255, "TEC saturates at 255");
    TEST_ASSERT_EQ(mcp2515_err_state(&m), MCP_ERR_STATE_BUSOFF, "state == bus-off");
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_EFLG) & MCP_EFLG_TXBO, "TXBO set");
    TEST_ASSERT(mcp2515_is_busoff(&m), "is_busoff helper agrees");
    return 0;
}

int test_force_busoff_helper(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_force_busoff(&m);
    TEST_ASSERT(mcp2515_is_busoff(&m), "force_busoff puts us there");
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_ERRIF,
                "ERRIF set on force_busoff");
    return 0;
}

int test_force_error_passive_helper(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_force_error_passive(&m);
    TEST_ASSERT_EQ(mcp2515_err_state(&m), MCP_ERR_STATE_PASSIVE,
                   "force_error_passive sets passive");
    return 0;
}

int test_busoff_disables_tx(void)
{
    /* Stage a TX in NORMAL mode, then force bus-off, then RTS — TXREQ
     * should stay set (no transmission). */
    mcp2515_t m; mcp2515_init(&m, "t");
    open_filters(&m);
    set_mode_normal(&m);
    mcp2515_force_busoff(&m);

    /* Stage minimal frame in TXB0. */
    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0);
    mcp2515_reg_write(&m, MCP_TXB0DLC,  0);

    spi_rts(&m, 0x01);

    TEST_ASSERT(mcp2515_reg_read(&m, MCP_TXB0CTRL) & MCP_TXBCTRL_TXREQ,
                "TXREQ stays pending in bus-off");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_TX0IF),
                "TX0IF NOT set (no transmission)");
    return 0;
}

int test_busoff_disables_rx(void)
{
    /* Inbound frames are dropped while in bus-off. */
    mcp2515_t m; mcp2515_init(&m, "t");
    open_filters(&m);
    set_mode_normal(&m);
    mcp2515_force_busoff(&m);

    mcp2515_frame_t f = {0};
    f.id  = 0x123;
    f.dlc = 1;
    f.data[0] = 0xAA;

    bool accepted = mcp2515_rx_frame(&m, &f);
    TEST_ASSERT(!accepted, "rx_frame rejected in bus-off");
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_CANINTF) & MCP_INT_RX0IF),
                "RX0IF NOT set");
    return 0;
}

int test_recover_via_helper(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    mcp2515_force_busoff(&m);
    TEST_ASSERT(mcp2515_is_busoff(&m), "in bus-off");

    mcp2515_recover_busoff(&m);
    TEST_ASSERT(!mcp2515_is_busoff(&m), "recovered");
    TEST_ASSERT_EQ(mcp2515_tec(&m), 0, "TEC cleared");
    TEST_ASSERT_EQ(mcp2515_rec(&m), 0, "REC cleared");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&m, MCP_EFLG) & MCP_EFLG_TXBO, 0,
                       "TXBO cleared");
    return 0;
}

int test_recover_via_canctrl_mode_change(void)
{
    /* The firmware-driven recovery path: write a different REQOP into
     * CANCTRL, which our model treats as a manual bus-off recovery. */
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);
    mcp2515_force_busoff(&m);
    TEST_ASSERT(mcp2515_is_busoff(&m), "bus-off after force");

    /* Toggle through Configuration. */
    spi_write_one(&m, MCP_CANCTRL,
                  (uint8_t)MCP_MODE_CONFIG << MCP_CANCTRL_REQOP_SHIFT);
    TEST_ASSERT(!mcp2515_is_busoff(&m), "Config-mode write recovers from bus-off");
    TEST_ASSERT_EQ(mcp2515_tec(&m), 0, "TEC reset");
    return 0;
}

int test_successful_tx_decrements_tec(void)
{
    /* TEC should drop by 1 each successful transmission. */
    mcp2515_t m; mcp2515_init(&m, "t");
    open_filters(&m);
    set_mode_loopback(&m);

    /* Bump TEC to a known value. */
    mcp2515_inject_tx_errors(&m, 5);    /* TEC = 40 */
    TEST_ASSERT_EQ(mcp2515_tec(&m), 40, "TEC=40 after inject");

    mcp2515_reg_write(&m, MCP_TXB0SIDH, 0);
    mcp2515_reg_write(&m, MCP_TXB0DLC,  0);
    spi_rts(&m, 0x01);

    TEST_ASSERT_EQ(mcp2515_tec(&m), 39, "TEC decrements after successful TX");
    return 0;
}

int test_successful_rx_decrements_rec(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    open_filters(&m);
    set_mode_normal(&m);

    mcp2515_inject_rx_errors(&m, 10);
    TEST_ASSERT_EQ(mcp2515_rec(&m), 10, "REC=10 after inject");

    mcp2515_frame_t f = {0};
    f.id  = 0x100;
    f.dlc = 0;
    bool ok = mcp2515_rx_frame(&m, &f);
    TEST_ASSERT(ok, "frame accepted");
    TEST_ASSERT_EQ(mcp2515_rec(&m), 9, "REC decrements on successful RX");
    return 0;
}

int test_eflg_overflow_sticky_on_state_update(void)
{
    /* RX0OVR should survive TEC/REC updates — it's cleared only by
     * firmware via BIT MODIFY. */
    mcp2515_t m; mcp2515_init(&m, "t");
    open_filters(&m);
    set_mode_normal(&m);

    /* Manually set the overflow bit, then trigger an update via TEC. */
    uint8_t eflg = mcp2515_reg_read(&m, MCP_EFLG);
    mcp2515_reg_write(&m, MCP_EFLG, eflg | MCP_EFLG_RX0OVR);
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_EFLG) & MCP_EFLG_RX0OVR, "RX0OVR set");

    mcp2515_inject_tx_errors(&m, 1);
    TEST_ASSERT(mcp2515_reg_read(&m, MCP_EFLG) & MCP_EFLG_RX0OVR,
                "RX0OVR sticky across error-state recompute");
    return 0;
}
