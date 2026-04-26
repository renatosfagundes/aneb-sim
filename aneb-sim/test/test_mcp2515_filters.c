/*
 * test_mcp2515_filters.c — acceptance filter and mask logic.
 *
 * Helpers in this file program filter/mask quartets directly via the public
 * register API rather than through SPI WRITE — keeps the asserts focused on
 * the filtering algorithm rather than SPI plumbing.
 */
#include "test.h"
#include "test_helpers.h"
#include "mcp2515.h"
#include "mcp2515_regs.h"

static void set_filter_std(mcp2515_t *m, int idx, uint16_t id)
{
    /* Per datasheet 4.2: SIDH = id[10:3], SIDL = id[2:0]<<5, EID = 0,
     * EXIDE clear. */
    static const uint8_t base[6] = {
        MCP_RXF0SIDH, MCP_RXF1SIDH, MCP_RXF2SIDH,
        MCP_RXF3SIDH, MCP_RXF4SIDH, MCP_RXF5SIDH,
    };
    uint8_t a = base[idx];
    mcp2515_reg_write(m, a + 0, (uint8_t)((id >> 3) & 0xFF));
    mcp2515_reg_write(m, a + 1, (uint8_t)((id & 0x07) << 5));
    mcp2515_reg_write(m, a + 2, 0);
    mcp2515_reg_write(m, a + 3, 0);
}

static void set_filter_ext(mcp2515_t *m, int idx, uint32_t id)
{
    static const uint8_t base[6] = {
        MCP_RXF0SIDH, MCP_RXF1SIDH, MCP_RXF2SIDH,
        MCP_RXF3SIDH, MCP_RXF4SIDH, MCP_RXF5SIDH,
    };
    uint8_t a = base[idx];
    mcp2515_reg_write(m, a + 0, (uint8_t)((id >> 21) & 0xFF));
    mcp2515_reg_write(m, a + 1, (uint8_t)(((id >> 18) & 0x07) << 5)
                              | MCP_SIDL_EXIDE
                              | (uint8_t)((id >> 16) & 0x03));
    mcp2515_reg_write(m, a + 2, (uint8_t)((id >> 8) & 0xFF));
    mcp2515_reg_write(m, a + 3, (uint8_t)(id & 0xFF));
}

static void set_mask_std_exact(mcp2515_t *m, int rxm /*0 or 1*/)
{
    /* All 11 SID bits significant. */
    uint8_t base = (rxm == 0) ? MCP_RXM0SIDH : MCP_RXM1SIDH;
    mcp2515_reg_write(m, base + 0, 0xFF);
    mcp2515_reg_write(m, base + 1, 0xE0);
    mcp2515_reg_write(m, base + 2, 0x00);
    mcp2515_reg_write(m, base + 3, 0x00);
}

static void set_mask_zero(mcp2515_t *m, int rxm)
{
    uint8_t base = (rxm == 0) ? MCP_RXM0SIDH : MCP_RXM1SIDH;
    for (int i = 0; i < 4; i++) mcp2515_reg_write(m, base + i, 0x00);
}

static void set_mask_ext_exact(mcp2515_t *m, int rxm)
{
    /* All 29 bits significant. */
    uint8_t base = (rxm == 0) ? MCP_RXM0SIDH : MCP_RXM1SIDH;
    mcp2515_reg_write(m, base + 0, 0xFF);                     /* sid[10:3] */
    mcp2515_reg_write(m, base + 1, (0x07 << 5) | 0x03);       /* sid[2:0] + eid[17:16] */
    mcp2515_reg_write(m, base + 2, 0xFF);                     /* eid[15:8] */
    mcp2515_reg_write(m, base + 3, 0xFF);                     /* eid[7:0] */
}

static mcp2515_frame_t make_std(uint16_t id)
{
    mcp2515_frame_t f = {0};
    f.id  = id;
    f.ext = false;
    f.dlc = 4;
    f.data[0] = 0xDE; f.data[1] = 0xAD; f.data[2] = 0xBE; f.data[3] = 0xEF;
    return f;
}

static mcp2515_frame_t make_ext(uint32_t id)
{
    mcp2515_frame_t f = {0};
    f.id  = id & 0x1FFFFFFFu;
    f.ext = true;
    f.dlc = 1;
    f.data[0] = 0x42;
    return f;
}

int test_filter_exact_match_std(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    /* Constrain BOTH buffers — otherwise RXB1 with its default permissive
     * mask accepts everything and the "should NOT match" check never
     * actually exercises filtering. */
    set_mask_std_exact(&m, 0);
    set_mask_std_exact(&m, 1);
    set_filter_std(&m, 0, 0x123);
    /* RXF1..RXF5 default to 0; with exact masks, only id=0 matches them. */

    mcp2515_frame_t hit  = make_std(0x123);
    mcp2515_frame_t miss = make_std(0x124);

    TEST_ASSERT(mcp2515_rx_frame(&m, &hit),  "id 0x123 should match");
    mcp2515_reg_write(&m, MCP_CANINTF, 0);
    TEST_ASSERT(!mcp2515_rx_frame(&m, &miss), "id 0x124 should NOT match");
    return 0;
}

int test_filter_mask_zero_accepts_all(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    set_mask_zero(&m, 0);              /* don't-care */
    set_filter_std(&m, 0, 0x000);      /* irrelevant */

    /* Any standard frame matches; arbitrary ID. */
    mcp2515_frame_t f = make_std(0x456);
    TEST_ASSERT(mcp2515_rx_frame(&m, &f), "should accept any id with mask=0");
    return 0;
}

int test_filter_extended_id(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    /* Constrain both buffers — see test_filter_exact_match_std. */
    set_mask_ext_exact(&m, 0);
    set_mask_ext_exact(&m, 1);
    set_filter_ext(&m, 0, 0x1ABCDEFu);

    mcp2515_frame_t hit  = make_ext(0x1ABCDEFu);
    mcp2515_frame_t miss = make_ext(0x1ABCDEEu);

    TEST_ASSERT(mcp2515_rx_frame(&m, &hit),   "exact ext match");
    mcp2515_reg_write(&m, MCP_CANINTF, 0);
    TEST_ASSERT(!mcp2515_rx_frame(&m, &miss), "near-miss ext rejected");
    return 0;
}

int test_filter_std_vs_ext_disagreement(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    set_mask_std_exact(&m, 0);
    set_filter_std(&m, 0, 0x123);     /* standard filter */

    /* An extended frame whose top 11 bits match still must not pass a
     * standard filter — EXIDE must agree. */
    mcp2515_frame_t f_ext = make_ext(0x123u << 18);
    TEST_ASSERT(!mcp2515_rx_frame(&m, &f_ext),
                "ext frame must not satisfy std filter");
    return 0;
}

int test_rxm_recv_any_bypasses_filters(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    /* RXB0CTRL.RXM = 11 — receive any. */
    mcp2515_reg_write(&m, MCP_RXB0CTRL,
                      (uint8_t)MCP_RXM_RECV_ANY << MCP_RXB0CTRL_RXM_SHIFT);

    /* Even with no filters set up, ANY id should be accepted. */
    mcp2515_frame_t f = make_std(0x7FF);
    TEST_ASSERT(mcp2515_rx_frame(&m, &f),
                "RXM=any should accept regardless of filters");
    return 0;
}

int test_rxm_recv_std_rejects_ext(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    /* RXB0 standard-only; clear RXB1 (default = filtered+RXM=00). */
    mcp2515_reg_write(&m, MCP_RXB0CTRL,
                      (uint8_t)MCP_RXM_RECV_STD << MCP_RXB0CTRL_RXM_SHIFT);
    mcp2515_reg_write(&m, MCP_RXB1CTRL, 0x00);

    /* Set RXB0 to receive any STD frame (mask=0 with std filter); RXB1 to
     * receive any EXT frame (mask=0 with ext-mode + a filter). */
    set_mask_zero(&m, 0);
    set_mask_zero(&m, 1);
    set_filter_std(&m, 0, 0);
    set_filter_ext(&m, 2, 0);

    mcp2515_frame_t std = make_std(0x55);
    TEST_ASSERT(mcp2515_rx_frame(&m, &std),
                "RXM=std should accept std frame");

    /* RXB0 still busy with STD frame; ext frame should NOT land in RXB0. */
    mcp2515_frame_t ext = make_ext(0xABCDEu);
    TEST_ASSERT(mcp2515_rx_frame(&m, &ext),
                "ext frame routed to RXB1");
    /* RXB0 contents unchanged (still std). */
    TEST_ASSERT(!(mcp2515_reg_read(&m, MCP_RXB0SIDL) & MCP_SIDL_EXIDE),
                "RXB0 didn't get clobbered with ext");
    return 0;
}

int test_rxb0_filhit_recorded(void)
{
    mcp2515_t m; mcp2515_init(&m, "t");
    set_mode_normal(&m);

    set_mask_std_exact(&m, 0);
    set_filter_std(&m, 0, 0x100);
    set_filter_std(&m, 1, 0x200);

    mcp2515_frame_t a = make_std(0x100);
    mcp2515_frame_t b = make_std(0x200);

    TEST_ASSERT(mcp2515_rx_frame(&m, &a), "first filter hit");
    TEST_ASSERT_EQ(mcp2515_reg_read(&m, MCP_RXB0CTRL) & MCP_RXB0CTRL_FILHIT_MASK,
                   0, "FILHIT should be 0 (RXF0)");
    /* Free buffer for next test. */
    mcp2515_reg_write(&m, MCP_CANINTF, 0);

    TEST_ASSERT(mcp2515_rx_frame(&m, &b), "second filter hit");
    TEST_ASSERT_EQ(mcp2515_reg_read(&m, MCP_RXB0CTRL) & MCP_RXB0CTRL_FILHIT_MASK,
                   1, "FILHIT should be 1 (RXF1)");
    return 0;
}
