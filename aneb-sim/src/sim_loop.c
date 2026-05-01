#include "sim_loop.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "avr_uart.h"
#include "avr_spi.h"
#include "avr_timer.h"
#include "avr_twi.h"

#include "can_bus.h"
#include "chip.h"
#include "i2c_lcd.h"
#include "mcp2515.h"
#include "pin_names.h"
#include "proto.h"
#include "uart_server.h"

#define MCP_CS_PORT   'B'
#define MCP_CS_BIT     2
#define MCP_INT_PORT  'D'
#define MCP_INT_BIT    2

#define LCD_I2C_ADDR   0x27

/* ----- global state ---------------------------------------------------- */

static chip_t            g_chips[SIM_MAX_CHIPS];
static int               g_nchips     = 0;
static atomic_bool       g_stop_req   = false;
static atomic_bool       g_paused_all = false;
/* Default to real-time (1.0×).  Most demos — CAN, LCD, button work —
 * want millis()-paced behaviour out of the box.  Use the toolbar's Max
 * button or `--speed=0` to run unthrottled. */
static volatile double   g_speed      = 1.0;

/* One MCP2515 + LCD per ECU (indices 0..3). Declared early because
 * drain_can_rx and the thread function both reference them. */
static mcp2515_t  g_can[4];
static can_bus_t  g_can1;
static i2c_lcd_t  g_lcd[4];
static avr_irq_t *g_lcd_irq[4];

/* Cached UART input IRQs — populated in wire_chip_irqs(), used in
 * chip_thread_fn() to deliver TCP UART RX bytes into each chip. */
static avr_irq_t *g_uart_rx_irq[SIM_MAX_CHIPS];

/* ----- per-chip threads ------------------------------------------------ */

static pthread_t g_chip_threads[SIM_MAX_CHIPS];

/* ----- per-chip real-time pacing --------------------------------------- */

static void chip_pace_tick(chip_t *c)
{
    double speed = g_speed;
    if (speed <= 0.0) return;

    uint64_t sim_cycles = c->avr->cycle;
    uint32_t freq       = c->avr->frequency;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (!c->pace_init || sim_cycles < c->pace_sim0) {
        c->pace_wall0 = now;
        c->pace_sim0  = sim_cycles;
        c->pace_init  = true;
        return;
    }

    int64_t wall_ns = (int64_t)(now.tv_sec  - c->pace_wall0.tv_sec ) * 1000000000LL
                    + (int64_t)(now.tv_nsec - c->pace_wall0.tv_nsec);
    int64_t sim_ns  = (int64_t)(
        (double)(sim_cycles - c->pace_sim0) / (double)freq * 1e9 / speed);
    int64_t ahead_ns = sim_ns - wall_ns;
    if (ahead_ns > 1000000LL) {
        int64_t sleep_ns = ahead_ns;
        if (sleep_ns > 50000000LL) sleep_ns = 50000000LL;  /* cap at 50 ms */
        struct timespec ts = {
            .tv_sec  = sleep_ns / 1000000000LL,
            .tv_nsec = sleep_ns % 1000000000LL,
        };
        nanosleep(&ts, NULL);
    }
}

/* avr->sleep callback: advance cycle counter without sleeping inside
 * avr_lock.  chip_pace_tick() after the batch does the wall-clock sleep. */
static void chip_avr_sleep(avr_t *avr, avr_cycle_count_t how_long)
{
    avr->cycle += how_long;
}

/* ----- incoming CAN frame queue per ECU ------------------------------- */

#define CHIP_CAN_RX_CAP 32

typedef struct {
    mcp2515_frame_t frames[CHIP_CAN_RX_CAP];
    int             head, tail, count;
    pthread_mutex_t lock;
} chip_can_rx_t;

static chip_can_rx_t g_can_rx[4];

/* Enqueue a CAN frame for chip `idx` from any thread. */
static void chip_can_rx_enqueue(int idx, const mcp2515_frame_t *frame)
{
    chip_can_rx_t *q = &g_can_rx[idx];
    pthread_mutex_lock(&q->lock);
    if (q->count < CHIP_CAN_RX_CAP) {
        q->frames[q->tail] = *frame;
        q->tail = (q->tail + 1) % CHIP_CAN_RX_CAP;
        q->count++;
    }
    pthread_mutex_unlock(&q->lock);
}

/* Drain and deliver pending CAN frames for chip `idx`.
 * Must be called from the chip's own thread (before the avr_run batch). */
static void drain_can_rx(int idx)
{
    chip_can_rx_t *q = &g_can_rx[idx];
    chip_t        *c = &g_chips[idx];

    pthread_mutex_lock(&q->lock);
    while (q->count > 0) {
        mcp2515_frame_t f = q->frames[q->head];
        q->head = (q->head + 1) % CHIP_CAN_RX_CAP;
        q->count--;
        pthread_mutex_unlock(&q->lock);

        /* Deliver under avr_lock — mcp2515_rx_frame calls on_mcp_int which
         * raises an IRQ on this chip's avr_t. */
        pthread_mutex_lock(&c->avr_lock);
        if (mcp2515_rx_frame(&g_can[idx], &f))
            g_can1.frames_delivered++;
        pthread_mutex_unlock(&c->avr_lock);

        pthread_mutex_lock(&q->lock);
    }
    pthread_mutex_unlock(&q->lock);
}

/* ----- chip simulation thread ----------------------------------------- */

static void *chip_thread_fn(void *arg)
{
    int     idx     = (int)(intptr_t)arg;
    chip_t *c       = &g_chips[idx];
    bool    has_mcp = (idx < 4);

    while (!atomic_load(&g_stop_req)) {
        if (has_mcp) drain_can_rx(idx);

        /* Reset-on-connect: emulates the DTR pulse that real Arduinos get
         * when avrdude (or any client) opens the COM port.  This lets
         * avrdude catch Optiboot's startup window without manual UI clicks.
         * Set sim speed to 1.0 before flashing so the bootloader runs at
         * real time and avrdude's STK500 timeouts are met. */
        if (uart_server_pop_connect(idx)) {
            pthread_mutex_lock(&c->avr_lock);
            if (c->running) {
                chip_reset(c);
                c->pace_init = false;
                proto_emit_log("info",
                               "chip %s: TCP client connected — chip reset",
                               c->id);
            }
            pthread_mutex_unlock(&c->avr_lock);
        }

        if (atomic_load(&g_paused_all) || !c->running || c->paused) {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
            continue;
        }

        pthread_mutex_lock(&c->avr_lock);
        /* Deliver bytes that arrived on the TCP UART socket, paced at
         * the UART byte rate so simavr's 64-byte input FIFO can drain
         * between pushes.  Without pacing, a single 133-byte STK_PROG_PAGE
         * from avrdude lands in our ring as one chunk and would raise
         * 133 IRQs in this batch — 69 of them get dropped (RX overrun)
         * and Optiboot reads a corrupt page header. */
        if (g_uart_rx_irq[idx]) {
            /* 16 MHz / (115200 baud / 10 bits-per-byte) ≈ 1389 cycles. */
            const uint64_t UART_CYCLES_PER_BYTE = 1400;
            uint8_t rb;
            while (c->avr->cycle >= c->uart_rx_due_cycle
                    && uart_server_pop_rx(idx, &rb)) {
                avr_raise_irq(g_uart_rx_irq[idx], rb);
                c->uart_rx_due_cycle = c->avr->cycle + UART_CYCLES_PER_BYTE;
            }
        }
        if (c->running && !c->paused) {
            for (int n = 0; n < SIM_CYCLES_PER_TICK; n++) {
                int s = chip_step(c);
                if (s == cpu_Done || s == cpu_Crashed) {
                    c->running = false;
                    proto_emit_log("warn", "chip %s: stopped (state=%d)",
                                   c->id, s);
                    break;
                }
            }
        }
        pthread_mutex_unlock(&c->avr_lock);

        /* Flush any UART TX bytes that were pushed during the batch. */
        uart_server_flush_tx(idx);

        if (c->running) chip_pace_tick(c);
    }
    return NULL;
}

/* ----- IRQ callbacks --------------------------------------------------- */

typedef struct {
    chip_t *chip;
    char    port;
    int     bit;
    /* Rate-limit state: last emitted value + emit timestamp.  Without
     * this, firmware that toggles a pin in a tight loop (e.g. mcp_can
     * banging CS at every SPI byte during a stuck begin() retry loop)
     * floods the UI with thousands of pin events per second, growing
     * Python's signal queue until QML stops responding. */
    int             last_val;
    struct timespec last_emit;
    bool            emit_pending;
} pin_ctx_t;

static pin_ctx_t g_pin_ctx[SIM_MAX_CHIPS * 3 * 8];
static int       g_pin_ctx_n = 0;

/* Cap pin event emission to ~100 Hz per (chip, pin).  Slow user-visible
 * toggles (button presses, LED blinks, PWM duty changes) are well below
 * this and pass through; bus-clock-rate toggles get throttled. */
#define PIN_RATE_LIMIT_NS  10000000   /* 10 ms */

static void on_pin_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    pin_ctx_t *ctx = (pin_ctx_t *)param;
    int v = value ? 1 : 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t dt_ns = (int64_t)(now.tv_sec  - ctx->last_emit.tv_sec ) * 1000000000LL
                  + (int64_t)(now.tv_nsec - ctx->last_emit.tv_nsec);

    /* Drop redundant same-value notifications outright (some simavr
     * IO modules fire the IRQ on every PORT write even when nothing
     * changed). */
    if (v == ctx->last_val && ctx->last_emit.tv_sec != 0) {
        return;
    }
    if (dt_ns < PIN_RATE_LIMIT_NS && ctx->last_emit.tv_sec != 0) {
        /* Too soon since last emit — skip this transition.  We'll lose
         * intermediate states; the UI will see the next emitted value
         * after the cooldown. */
        return;
    }

    ctx->last_val = v;
    ctx->last_emit = now;
    proto_emit_pin(ctx->chip->id,
                   pin_format(ctx->port, ctx->bit),
                   v,
                   ctx->chip->cycles);
}

static void on_uart_byte(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    chip_t *c = (chip_t *)param;
    uint8_t b = (uint8_t)value;
    int idx = (int)(c - g_chips);
    /* When a TCP client (e.g. avrdude) is attached, the UART bytes are its
     * binary protocol — emitting one JSON-Lines `uart` event per byte
     * floods QML's serial console and freezes the UI thread for 10–20 s
     * during a flash+verify (~10K bytes back-to-back).  Skip the JSON path
     * while a client is connected; the byte still goes to TCP. */
    if (!uart_server_has_client(idx))
        proto_emit_uart(c->id, &b, 1, c->cycles);
    /* also push into the per-chip TCP UART server TX buffer (flushed after
     * avr_lock is released in chip_thread_fn) */
    uart_server_push_tx(idx, b);
}

typedef struct {
    chip_t  *chip;
    char     port;
    int      bit;
    uint16_t top;
} pwm_ctx_t;

static pwm_ctx_t g_pwm_ctx[SIM_MAX_CHIPS * 4];
static int       g_pwm_ctx_n = 0;

static void on_pwm(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    pwm_ctx_t *ctx = (pwm_ctx_t *)param;
    double duty = (ctx->top > 0) ? (double)value / (double)ctx->top : 0.0;
    if (duty < 0.0) duty = 0.0;
    if (duty > 1.0) duty = 1.0;
    proto_emit_pwm(ctx->chip->id, pin_format(ctx->port, ctx->bit),
                   duty, ctx->chip->cycles);
}

static void wire_one_pwm(chip_t *c, char timer_letter, int pwm_index,
                         char port, int bit, uint16_t top)
{
    avr_irq_t *irq = avr_io_getirq(c->avr,
                                   AVR_IOCTL_TIMER_GETIRQ(timer_letter),
                                   TIMER_IRQ_OUT_PWM0 + pwm_index);
    if (!irq) return;
    pwm_ctx_t *ctx = &g_pwm_ctx[g_pwm_ctx_n++];
    ctx->chip = c;
    ctx->port = port;
    ctx->bit  = bit;
    ctx->top  = top;
    avr_irq_register_notify(irq, on_pwm, ctx);
}

static void wire_pwm_outputs(chip_t *c)
{
    wire_one_pwm(c, '0', 0, 'D', 6, 255);
    wire_one_pwm(c, '0', 1, 'D', 5, 255);
    wire_one_pwm(c, '1', 0, 'B', 1, 255);
    wire_one_pwm(c, '2', 1, 'D', 3, 255);
}

/* ----- MCP2515 <-> simavr glue ---------------------------------------- */

/* simavr fires SPI_IRQ_OUTPUT when the AVR writes SPDR (the byte the AVR
 * is transmitting to the external slave).  We listen on OUTPUT, feed the
 * byte through the MCP2515 model, then raise SPI_IRQ_INPUT with the
 * response so simavr loads it into SPDR for the AVR to read. */
static void on_mcp_spi_byte(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    int idx = (int)(intptr_t)param;
    if (idx < 0 || idx >= 4) return;
    chip_t    *c = &g_chips[idx];
    mcp2515_t *m = &g_can[idx];

    uint8_t out = mcp2515_spi_byte(m, (uint8_t)(value & 0xFF));

    avr_irq_t *spi_in = avr_io_getirq(c->avr,
                                      AVR_IOCTL_SPI_GETIRQ(0),
                                      SPI_IRQ_INPUT);
    if (spi_in) avr_raise_irq(spi_in, out);
}

static void on_mcp_cs_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    int idx = (int)(intptr_t)param;
    if (idx < 0 || idx >= 4) return;
    mcp2515_t *m = &g_can[idx];
    if (value == 0) mcp2515_cs_low(m);
    else            mcp2515_cs_high(m);
}

static void on_mcp_int(void *ctx, int asserted)
{
    int idx = (int)(intptr_t)ctx;
    if (idx < 0 || idx >= 4) return;
    chip_t *c = &g_chips[idx];
    avr_irq_t *int_pin = avr_io_getirq(c->avr,
                                       AVR_IOCTL_IOPORT_GETIRQ(MCP_INT_PORT),
                                       MCP_INT_BIT);
    if (int_pin) avr_raise_irq(int_pin, asserted ? 0 : 1);
}

/* Called when chip[idx]'s MCP2515 transmits a frame, inside avr_run() with
 * g_chips[idx].avr_lock held.  Enqueue into each peer's can_rx queue rather
 * than calling avr_raise_irq on a different chip's avr_t (which would race). */
static void on_mcp_tx(void *ctx, const mcp2515_frame_t *frame)
{
    int idx = (int)(intptr_t)ctx;
    if (idx < 0 || idx >= 4) return;
    chip_t *c = &g_chips[idx];

    proto_emit_can_tx(c->id, g_can1.name, frame, c->cycles);
    g_can1.frames_broadcast++;

    for (int i = 0; i < 4; i++) {
        if (i == idx) continue;
        chip_can_rx_enqueue(i, frame);
    }
}

static void wire_mcp2515(int idx)
{
    chip_t    *c = &g_chips[idx];
    mcp2515_t *m = &g_can[idx];

    char id[CHIP_ID_MAX + 8];
    snprintf(id, sizeof(id), "%.*s.can1", (int)(CHIP_ID_MAX - 1), c->id);
    mcp2515_init(m, id);
    m->on_int = on_mcp_int;
    m->on_tx  = on_mcp_tx;
    m->ctx    = (void *)(intptr_t)idx;
    can_bus_attach(&g_can1, m);

    /* Listen on OUTPUT (bytes the AVR is transmitting); the handler
     * processes them through the MCP2515 model and raises INPUT with
     * the response so simavr presents it back to the AVR via SPDR. */
    avr_irq_t *spi_out = avr_io_getirq(c->avr,
                                       AVR_IOCTL_SPI_GETIRQ(0),
                                       SPI_IRQ_OUTPUT);
    if (spi_out) {
        avr_irq_register_notify(spi_out, on_mcp_spi_byte,
                                (void *)(intptr_t)idx);
    }

    avr_irq_t *cs = avr_io_getirq(c->avr,
                                  AVR_IOCTL_IOPORT_GETIRQ(MCP_CS_PORT),
                                  MCP_CS_BIT);
    if (cs) {
        avr_irq_register_notify(cs, on_mcp_cs_change,
                                (void *)(intptr_t)idx);
    }

    proto_emit_log("info", "wired %s.can1 (CS=P%c%d INT=P%c%d)",
                   c->id, MCP_CS_PORT, MCP_CS_BIT,
                   MCP_INT_PORT, MCP_INT_BIT);
}

/* ----- I2C LCD <-> simavr glue ---------------------------------------- */

static void on_lcd_changed(void *ctx, const char *line0, const char *line1)
{
    int idx = (int)(intptr_t)ctx;
    if (idx < 0 || idx >= 4) return;
    chip_t *c = &g_chips[idx];
    proto_emit_lcd(c->id, line0, line1, c->cycles);
}

static void on_twi_msg(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    int idx = (int)(intptr_t)param;
    if (idx < 0 || idx >= 4) return;

    avr_twi_msg_irq_t v = {.u = {.v = value}};
    i2c_lcd_t *lcd = &g_lcd[idx];
    avr_irq_t *to_avr = g_lcd_irq[idx] + TWI_IRQ_INPUT;

    static bool lcd_selected[4] = {false, false, false, false};

    if (v.u.twi.msg & TWI_COND_STOP) {
        lcd_selected[idx] = false;
    }
    if (v.u.twi.msg & TWI_COND_START) {
        uint8_t addr_7bit = (uint8_t)(v.u.twi.addr >> 1);
        bool    is_write  = (v.u.twi.addr & 1) == 0;
        if (addr_7bit == lcd->i2c_addr && is_write) {
            lcd_selected[idx] = true;
            avr_raise_irq(to_avr,
                avr_twi_irq_msg(TWI_COND_ACK, v.u.twi.addr, 1));
        } else {
            lcd_selected[idx] = false;
        }
    }
    if (lcd_selected[idx] && (v.u.twi.msg & TWI_COND_WRITE)) {
        i2c_lcd_write_byte(lcd, v.u.twi.data);
        avr_raise_irq(to_avr,
            avr_twi_irq_msg(TWI_COND_ACK, v.u.twi.addr, 1));
    }
}

static const char *_lcd_irq_names[2] = {
    [TWI_IRQ_INPUT]  = "8>lcd.tx",
    [TWI_IRQ_OUTPUT] = "32<lcd.rx",
};

static void wire_lcd(int idx)
{
    chip_t    *c   = &g_chips[idx];
    i2c_lcd_t *lcd = &g_lcd[idx];

    char id[CHIP_ID_MAX + 8];
    snprintf(id, sizeof(id), "%.*s.lcd", (int)(CHIP_ID_MAX - 1), c->id);
    i2c_lcd_init(lcd, id, LCD_I2C_ADDR);
    lcd->on_changed = on_lcd_changed;
    lcd->ctx        = (void *)(intptr_t)idx;

    avr_irq_t *irq = avr_alloc_irq(&c->avr->irq_pool, 0, 2, _lcd_irq_names);
    g_lcd_irq[idx] = irq;
    avr_irq_register_notify(irq + TWI_IRQ_OUTPUT, on_twi_msg,
                            (void *)(intptr_t)idx);

    avr_irq_t *avr_twi_in  = avr_io_getirq(c->avr, AVR_IOCTL_TWI_GETIRQ(0),
                                           TWI_IRQ_INPUT);
    avr_irq_t *avr_twi_out = avr_io_getirq(c->avr, AVR_IOCTL_TWI_GETIRQ(0),
                                           TWI_IRQ_OUTPUT);
    if (!avr_twi_in)  avr_twi_in  = avr_io_getirq(c->avr, AVR_IOCTL_TWI_GETIRQ('0'),
                                                  TWI_IRQ_INPUT);
    if (!avr_twi_out) avr_twi_out = avr_io_getirq(c->avr, AVR_IOCTL_TWI_GETIRQ('0'),
                                                  TWI_IRQ_OUTPUT);
    if (avr_twi_in)  avr_connect_irq(irq + TWI_IRQ_INPUT,  avr_twi_in);
    if (avr_twi_out) avr_connect_irq(avr_twi_out, irq + TWI_IRQ_OUTPUT);

    proto_emit_log("info", "wired %s.lcd (I2C 0x%02x, twi_in=%s twi_out=%s)",
                   c->id, LCD_I2C_ADDR,
                   avr_twi_in  ? "ok" : "MISSING",
                   avr_twi_out ? "ok" : "MISSING");
}

static void wire_chip_irqs(chip_t *c)
{
    static const struct { char port; int bit; } pwm_pins[] = {
        {'D', 3}, {'D', 5}, {'D', 6}, {'B', 1},
    };
    static const char ports[] = {'B', 'C', 'D'};
    for (size_t p = 0; p < sizeof(ports); p++) {
        char port = ports[p];
        for (int bit = 0; bit < 8; bit++) {
            bool is_pwm = false;
            for (size_t k = 0; k < sizeof(pwm_pins)/sizeof(*pwm_pins); k++) {
                if (pwm_pins[k].port == port && pwm_pins[k].bit == bit) {
                    is_pwm = true; break;
                }
            }
            if (is_pwm) continue;
            avr_irq_t *irq = avr_io_getirq(c->avr,
                                           AVR_IOCTL_IOPORT_GETIRQ(port),
                                           bit);
            if (!irq) continue;
            pin_ctx_t *ctx = &g_pin_ctx[g_pin_ctx_n++];
            ctx->chip = c;
            ctx->port = port;
            ctx->bit  = bit;
            avr_irq_register_notify(irq, on_pin_change, ctx);
        }
    }

    uint32_t flags = 0;
    avr_ioctl(c->avr, AVR_IOCTL_UART_GET_FLAGS('0'), &flags);
    flags &= ~AVR_UART_FLAG_STDIO;
    avr_ioctl(c->avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);

    avr_irq_t *uart_out = avr_io_getirq(c->avr,
                                        AVR_IOCTL_UART_GETIRQ('0'),
                                        UART_IRQ_OUTPUT);
    if (uart_out) {
        avr_irq_register_notify(uart_out, on_uart_byte, c);
    }

    /* Cache the UART input IRQ so chip_thread_fn can deliver TCP RX bytes
     * without calling avr_io_getirq (which walks a linked list) per byte. */
    int cidx = (int)(c - g_chips);
    if (cidx >= 0 && cidx < SIM_MAX_CHIPS) {
        g_uart_rx_irq[cidx] = avr_io_getirq(c->avr,
                                             AVR_IOCTL_UART_GETIRQ('0'),
                                             UART_IRQ_INPUT);
    }

    wire_pwm_outputs(c);
}

/* ----- public API ------------------------------------------------------ */

int sim_loop_init(void)
{
    static const struct {
        const char *id;
        const char *mcu;
    } roster[SIM_MAX_CHIPS] = {
        {"ecu1", "atmega328p"},
        {"ecu2", "atmega328p"},
        {"ecu3", "atmega328p"},
        {"ecu4", "atmega328p"},
        {"mcu",  "atmega328pb"},
    };

    g_nchips = 0;
    for (int i = 0; i < SIM_MAX_CHIPS; i++) {
        if (chip_init(&g_chips[i], roster[i].id, roster[i].mcu) != 0) {
            return -1;
        }
        g_chips[i].avr->sleep = chip_avr_sleep;
        wire_chip_irqs(&g_chips[i]);
        g_nchips++;
    }

    for (int i = 0; i < 4; i++) {
        memset(&g_can_rx[i], 0, sizeof(g_can_rx[i]));
        pthread_mutex_init(&g_can_rx[i].lock, NULL);
    }

    can_bus_init(&g_can1, "can1");
    for (int i = 0; i < 4; i++) wire_mcp2515(i);
    for (int i = 0; i < 4; i++) wire_lcd(i);

    if (uart_server_init(g_nchips) != 0) {
        proto_emit_log("warn",
                       "uart_server_init failed — TCP UART ports unavailable");
        /* non-fatal: simulation continues without TCP UART */
    }

    proto_emit_log("info",
                   "sim_loop: initialized %d chips, 4 mcp2515, 4 lcd, bus '%s'",
                   g_nchips, g_can1.name);
    return 0;
}

int sim_loop_start(void)
{
    if (uart_server_start() != 0) {
        proto_emit_log("warn", "uart_server_start failed — TCP UART unavailable");
    }

    for (int i = 0; i < g_nchips; i++) {
        if (pthread_create(&g_chip_threads[i], NULL, chip_thread_fn,
                           (void *)(intptr_t)i) != 0) {
            proto_emit_log("error",
                           "sim_loop_start: failed to create thread %d", i);
            return -1;
        }
    }
    proto_emit_log("info", "sim_loop: started %d chip threads", g_nchips);
    return 0;
}

chip_t *sim_loop_find(const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < g_nchips; i++) {
        if (strcmp(g_chips[i].id, id) == 0) return &g_chips[i];
    }
    return NULL;
}

chip_t *sim_loop_chip(int index)
{
    if (index < 0 || index >= g_nchips) return NULL;
    return &g_chips[index];
}

int sim_loop_count(void) { return g_nchips; }

struct can_bus *sim_loop_bus(void) { return &g_can1; }

void sim_loop_can_inject(const mcp2515_frame_t *frame)
{
    if (!frame) return;
    g_can1.frames_injected++;
    g_can1.frames_broadcast++;
    for (int i = 0; i < 4; i++) chip_can_rx_enqueue(i, frame);
}

void sim_loop_pause_all(void)
{
    atomic_store(&g_paused_all, true);
    for (int i = 0; i < g_nchips; i++) {
        pthread_mutex_lock(&g_chips[i].avr_lock);
        g_chips[i].pace_init = false;
        pthread_mutex_unlock(&g_chips[i].avr_lock);
    }
}

void sim_loop_resume_all(void)  { atomic_store(&g_paused_all, false); }

void sim_loop_set_speed(double factor)
{
    g_speed = factor;
    for (int i = 0; i < g_nchips; i++) {
        pthread_mutex_lock(&g_chips[i].avr_lock);
        g_chips[i].pace_init = false;
        pthread_mutex_unlock(&g_chips[i].avr_lock);
    }
}

void sim_loop_request_stop(void) { atomic_store(&g_stop_req, true); }
bool sim_loop_should_stop(void)  { return atomic_load(&g_stop_req); }

void sim_loop_shutdown(void)
{
    atomic_store(&g_stop_req, true);
    uart_server_shutdown();
    for (int i = 0; i < g_nchips; i++) pthread_join(g_chip_threads[i], NULL);
    for (int i = 0; i < 4; i++) pthread_mutex_destroy(&g_can_rx[i].lock);
    for (int i = 0; i < g_nchips; i++) chip_free(&g_chips[i]);
    g_nchips = 0;
}
