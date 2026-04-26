/*
 * test_can_bus.c — multi-node CAN bus routing.
 *
 * Pure-C: instantiates several mcp2515_t controllers, attaches them to
 * a can_bus_t, broadcasts/injects frames, and asserts on which buffers
 * received what. No simavr, no AVR cycles.
 */
#include "test.h"
#include "test_helpers.h"
#include "can_bus.h"
#include "mcp2515.h"
#include "mcp2515_regs.h"

/* Shim used by test_bus_normal_tx_through_callback so a controller's
 * on_tx callback forwards into a bus. */
typedef struct {
    can_bus_t *bus;
    mcp2515_t *self;
} bus_ctx_t;

static void on_tx_to_bus(void *ctx, const mcp2515_frame_t *f)
{
    bus_ctx_t *cx = (bus_ctx_t *)ctx;
    can_bus_broadcast(cx->bus, f, cx->self);
}

/* Open all filters on a controller so any frame is accepted. */
static void open_filters(mcp2515_t *m)
{
    for (int i = 0; i < 4; i++) {
        mcp2515_reg_write(m, MCP_RXM0SIDH + i, 0);
        mcp2515_reg_write(m, MCP_RXM1SIDH + i, 0);
    }
    set_mode_normal(m);
}

static mcp2515_frame_t make_frame(uint16_t id, uint8_t dlc)
{
    mcp2515_frame_t f = {0};
    f.id  = id;
    f.dlc = dlc;
    for (int i = 0; i < dlc; i++) f.data[i] = (uint8_t)(0xA0 + i);
    return f;
}

int test_bus_attach(void)
{
    can_bus_t bus;
    can_bus_init(&bus, "can1");
    TEST_ASSERT_EQ(bus.num_nodes, 0, "fresh bus is empty");

    mcp2515_t a, b, c;
    mcp2515_init(&a, "a"); mcp2515_init(&b, "b"); mcp2515_init(&c, "c");

    TEST_ASSERT_EQ(can_bus_attach(&bus, &a), 0, "first attach -> slot 0");
    TEST_ASSERT_EQ(can_bus_attach(&bus, &b), 1, "second attach -> slot 1");
    TEST_ASSERT_EQ(can_bus_attach(&bus, &c), 2, "third attach -> slot 2");
    TEST_ASSERT_EQ(bus.num_nodes, 3, "node count");

    /* Double-attach is rejected. */
    TEST_ASSERT_EQ(can_bus_attach(&bus, &a), -1, "double-attach refused");
    return 0;
}

int test_bus_broadcast_skips_source(void)
{
    can_bus_t bus;
    can_bus_init(&bus, "can1");

    mcp2515_t src, peer;
    mcp2515_init(&src,  "src");
    mcp2515_init(&peer, "peer");
    open_filters(&src);
    open_filters(&peer);
    can_bus_attach(&bus, &src);
    can_bus_attach(&bus, &peer);

    mcp2515_frame_t f = make_frame(0x123, 4);
    can_bus_broadcast(&bus, &f, &src);

    /* Source should NOT have RX0IF set — it's the originator. */
    TEST_ASSERT(!(mcp2515_reg_read(&src, MCP_CANINTF) & MCP_INT_RX0IF),
                "source must not self-receive in normal-mode broadcast");
    /* Peer should have RX0IF set. */
    TEST_ASSERT(mcp2515_reg_read(&peer, MCP_CANINTF) & MCP_INT_RX0IF,
                "peer should have received the frame");
    return 0;
}

int test_bus_broadcast_to_multiple_peers(void)
{
    can_bus_t bus;
    can_bus_init(&bus, "can1");

    mcp2515_t a, b, c, d;
    mcp2515_init(&a, "a"); open_filters(&a);
    mcp2515_init(&b, "b"); open_filters(&b);
    mcp2515_init(&c, "c"); open_filters(&c);
    mcp2515_init(&d, "d"); open_filters(&d);
    can_bus_attach(&bus, &a);
    can_bus_attach(&bus, &b);
    can_bus_attach(&bus, &c);
    can_bus_attach(&bus, &d);

    mcp2515_frame_t f = make_frame(0x456, 2);
    can_bus_broadcast(&bus, &f, &a);

    TEST_ASSERT(!(mcp2515_reg_read(&a, MCP_CANINTF) & MCP_INT_RX0IF), "src not self-rx");
    TEST_ASSERT(mcp2515_reg_read(&b, MCP_CANINTF) & MCP_INT_RX0IF,    "b received");
    TEST_ASSERT(mcp2515_reg_read(&c, MCP_CANINTF) & MCP_INT_RX0IF,    "c received");
    TEST_ASSERT(mcp2515_reg_read(&d, MCP_CANINTF) & MCP_INT_RX0IF,    "d received");
    TEST_ASSERT_EQ(bus.frames_delivered, 3, "3 successful deliveries");
    return 0;
}

int test_bus_inject_no_source_skip(void)
{
    can_bus_t bus;
    can_bus_init(&bus, "can1");

    mcp2515_t a, b;
    mcp2515_init(&a, "a"); open_filters(&a);
    mcp2515_init(&b, "b"); open_filters(&b);
    can_bus_attach(&bus, &a);
    can_bus_attach(&bus, &b);

    mcp2515_frame_t f = make_frame(0x789, 1);
    can_bus_inject(&bus, &f);

    /* Both nodes should receive — there's no source to skip. */
    TEST_ASSERT(mcp2515_reg_read(&a, MCP_CANINTF) & MCP_INT_RX0IF, "a got injected");
    TEST_ASSERT(mcp2515_reg_read(&b, MCP_CANINTF) & MCP_INT_RX0IF, "b got injected");
    TEST_ASSERT_EQ(bus.frames_injected, 1, "inject counter incremented");
    return 0;
}

int test_bus_filter_end_to_end(void)
{
    /* Two peers with selective filters: peer A accepts only id 0x100,
     * peer B accepts only id 0x200. A frame with id 0x100 should land
     * only on A. */
    can_bus_t bus;
    can_bus_init(&bus, "can1");

    mcp2515_t src, a, b;
    mcp2515_init(&src, "src"); open_filters(&src);
    mcp2515_init(&a,   "a");
    mcp2515_init(&b,   "b");

    set_mode_normal(&a);
    set_mode_normal(&b);
    /* RXM0 / RXM1 = 0xFFE0 (all 11 SID bits significant) on both peers. */
    mcp2515_reg_write(&a, MCP_RXM0SIDH, 0xFF);
    mcp2515_reg_write(&a, MCP_RXM0SIDL, 0xE0);
    mcp2515_reg_write(&a, MCP_RXM1SIDH, 0xFF);
    mcp2515_reg_write(&a, MCP_RXM1SIDL, 0xE0);
    mcp2515_reg_write(&b, MCP_RXM0SIDH, 0xFF);
    mcp2515_reg_write(&b, MCP_RXM0SIDL, 0xE0);
    mcp2515_reg_write(&b, MCP_RXM1SIDH, 0xFF);
    mcp2515_reg_write(&b, MCP_RXM1SIDL, 0xE0);

    /* a accepts id 0x100  (SIDH=0x20, SIDL=0x00) */
    mcp2515_reg_write(&a, MCP_RXF0SIDH, 0x20);
    mcp2515_reg_write(&a, MCP_RXF0SIDL, 0x00);
    /* b accepts id 0x200  (SIDH=0x40, SIDL=0x00) */
    mcp2515_reg_write(&b, MCP_RXF0SIDH, 0x40);
    mcp2515_reg_write(&b, MCP_RXF0SIDL, 0x00);

    can_bus_attach(&bus, &src);
    can_bus_attach(&bus, &a);
    can_bus_attach(&bus, &b);

    mcp2515_frame_t f = make_frame(0x100, 4);
    can_bus_broadcast(&bus, &f, &src);

    TEST_ASSERT(mcp2515_reg_read(&a, MCP_CANINTF) & MCP_INT_RX0IF,
                "a should accept 0x100");
    TEST_ASSERT(!(mcp2515_reg_read(&b, MCP_CANINTF) & MCP_INT_RX0IF),
                "b must NOT accept 0x100");
    TEST_ASSERT(!(mcp2515_reg_read(&b, MCP_CANINTF) & MCP_INT_RX1IF),
                "b RXB1 must also stay empty");
    return 0;
}

int test_bus_mode_gating(void)
{
    /* A peer in CONFIG mode (or SLEEP) must not accept inbound frames. */
    can_bus_t bus;
    can_bus_init(&bus, "can1");

    mcp2515_t src, peer;
    mcp2515_init(&src,  "src"); open_filters(&src);
    mcp2515_init(&peer, "peer");
    open_filters(&peer);    /* sets normal mode */
    mcp2515_reset(&peer);   /* and now back into Config */

    can_bus_attach(&bus, &src);
    can_bus_attach(&bus, &peer);

    mcp2515_frame_t f = make_frame(0x123, 1);
    can_bus_broadcast(&bus, &f, &src);

    TEST_ASSERT(!(mcp2515_reg_read(&peer, MCP_CANINTF) & MCP_INT_RX0IF),
                "Config-mode peer must not RX");
    TEST_ASSERT(!(mcp2515_reg_read(&peer, MCP_CANINTF) & MCP_INT_RX1IF),
                "Config-mode peer must not RX (RXB1)");
    return 0;
}

int test_bus_normal_tx_through_callback(void)
{
    /* Verify the MCP2515 NORMAL-mode TX path: writing TXREQ should fire
     * on_tx, which in real wiring forwards to the bus. We hook a shim
     * that does exactly the bus-broadcast call sim_loop performs. */
    can_bus_t bus;
    can_bus_init(&bus, "can1");

    mcp2515_t src, peer;
    mcp2515_init(&src,  "src"); open_filters(&src);
    mcp2515_init(&peer, "peer"); open_filters(&peer);

    bus_ctx_t cx = { &bus, &src };
    src.ctx   = &cx;
    src.on_tx = on_tx_to_bus;

    can_bus_attach(&bus, &src);
    can_bus_attach(&bus, &peer);

    /* Stage a frame and trigger TX. id 0x123, DLC 2, data=0xCAFE. */
    mcp2515_reg_write(&src, MCP_TXB0SIDH, 0x24);
    mcp2515_reg_write(&src, MCP_TXB0SIDL, 0x60);
    mcp2515_reg_write(&src, MCP_TXB0DLC,  0x02);
    mcp2515_reg_write(&src, MCP_TXB0D0,   0xCA);
    mcp2515_reg_write(&src, MCP_TXB0D0+1, 0xFE);
    spi_rts(&src, 0x01);

    TEST_ASSERT(!(mcp2515_reg_read(&src, MCP_TXB0CTRL) & MCP_TXBCTRL_TXREQ),
                "src TXREQ cleared");
    TEST_ASSERT(mcp2515_reg_read(&src, MCP_CANINTF) & MCP_INT_TX0IF,
                "src TX0IF set");
    TEST_ASSERT(mcp2515_reg_read(&peer, MCP_CANINTF) & MCP_INT_RX0IF,
                "peer RX0IF set after src TX");

    /* Verify peer received the actual frame contents. */
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&peer, MCP_RXB0DLC) & 0x0F, 2,
                       "peer DLC");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&peer, MCP_RXB0D0),   0xCA, "data[0]");
    TEST_ASSERT_EQ_HEX(mcp2515_reg_read(&peer, MCP_RXB0D0+1), 0xFE, "data[1]");
    return 0;
}
