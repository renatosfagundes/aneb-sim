#include "sim_loop.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

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

/* Board wiring per ECU: ANEB v1.1 has CS=PB2 (Arduino pin 10) and
 * INT=PD2 (Arduino INT0) for the CAN1 MCP2515 controller. */
#define MCP_CS_PORT   'B'
#define MCP_CS_BIT     2
#define MCP_INT_PORT  'D'
#define MCP_INT_BIT    2

/* I2C 1602 LCD on every ECU at the standard PCF8574 backpack address. */
#define LCD_I2C_ADDR   0x27

/* ----- module state ---------------------------------------------------- */

static chip_t            g_chips[SIM_MAX_CHIPS];
static int               g_nchips     = 0;
static atomic_bool       g_stop_req   = false;
static atomic_bool       g_paused_all = false;
static double            g_speed      = 1.0;

/* One MCP2515 per ECU (4); MCU has none. Indexed by chip index 0..3. */
static mcp2515_t         g_can[4];

/* Single shared CAN bus (CAN1). All four ECUs are attached. */
static can_bus_t         g_can1;

/* One I2C 1602 LCD per ECU. The slave's two TWI IRQs are also kept so
 * we can raise ACKs back at the master. */
static i2c_lcd_t         g_lcd[4];
static avr_irq_t        *g_lcd_irq[4];   /* base IRQ; +TWI_IRQ_INPUT/OUTPUT */

/* ----- IRQ callbacks --------------------------------------------------- */

typedef struct {
    chip_t *chip;
    char    port;
    int     bit;
} pin_ctx_t;

/* Statically allocated context array — 5 chips * 3 ports * 8 bits = 120. */
static pin_ctx_t g_pin_ctx[SIM_MAX_CHIPS * 3 * 8];
static int       g_pin_ctx_n = 0;

static void on_pin_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    pin_ctx_t *ctx = (pin_ctx_t *)param;
    proto_emit_pin(ctx->chip->id,
                   pin_format(ctx->port, ctx->bit),
                   value ? 1 : 0,
                   ctx->chip->cycles);
}

static void on_uart_byte(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    chip_t *c = (chip_t *)param;
    uint8_t b = (uint8_t)value;
    proto_emit_uart(c->id, &b, 1, c->cycles);
}

/* PWM event glue: simavr's TIMER_IRQ_OUT_PWMn fires (filtered) with the
 * raw OCR value whenever the firmware writes a different duty. We map
 * each (timer, channel) tuple to the AVR pin it physically drives and
 * emit a `pwm` event with duty = value / TOP. For the 8-bit timers
 * (Timer 0 / Timer 2) at fast-PWM with TOP = 255 (the analogWrite()
 * default), this is OCR / 255. */
typedef struct {
    chip_t  *chip;
    char     port;
    int      bit;
    uint16_t top;          /* PWM TOP value */
} pwm_ctx_t;

static pwm_ctx_t g_pwm_ctx[SIM_MAX_CHIPS * 4];   /* 4 PWM channels per chip */
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
    /* Map each PWM channel that does not collide with our board peripheral
     * wiring (MCP2515 SPI on PB2/PB3 disables Timer 1 OC1B and Timer 2
     * OC2A). TOPs default to 255 (fast PWM mode that analogWrite uses). */
    /* Timer 0: OC0A=PD6 (LDR_LED on ECU1), OC0B=PD5 (LOOP on ECU1) */
    wire_one_pwm(c, '0', 0, 'D', 6, 255);
    wire_one_pwm(c, '0', 1, 'D', 5, 255);
    /* Timer 1: OC1A=PB1 (Arduino 9, free) */
    wire_one_pwm(c, '1', 0, 'B', 1, 255);
    /* Timer 2: OC2B=PD3 (Arduino 3 = DOUT0, dimmable LED) */
    wire_one_pwm(c, '2', 1, 'D', 3, 255);
}

/* ----- MCP2515 <-> simavr glue ----------------------------------------
 *
 * One MCP2515 model per ECU, attached to:
 *   - the AVR's SPI peripheral (master TX byte triggers the model;
 *     model returns a byte that we feed back as master RX);
 *   - the CS GPIO pin (PB2) so we know when transactions begin and end;
 *   - the INT0 GPIO pin (PD2), which the model drives low when CANINTF
 *     bits are set and any are masked in CANINTE.
 *
 * Each callback uses the chip index (0..3) as its userdata so we can
 * dispatch back to the right MCP2515 instance and AVR core.
 */
static void on_mcp_spi_byte(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    int idx = (int)(intptr_t)param;
    if (idx < 0 || idx >= 4) return;
    chip_t    *c = &g_chips[idx];
    mcp2515_t *m = &g_can[idx];

    uint8_t out = mcp2515_spi_byte(m, (uint8_t)(value & 0xFF));

    avr_irq_t *spi_out = avr_io_getirq(c->avr,
                                       AVR_IOCTL_SPI_GETIRQ('0'),
                                       SPI_IRQ_OUTPUT);
    if (spi_out) avr_raise_irq(spi_out, out);
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
    /* INT is active LOW: asserted=1 means drive pin low (=0). */
    if (int_pin) avr_raise_irq(int_pin, asserted ? 0 : 1);
}

static void on_mcp_tx(void *ctx, const mcp2515_frame_t *frame)
{
    int idx = (int)(intptr_t)ctx;
    if (idx < 0 || idx >= 4) return;
    chip_t *c = &g_chips[idx];

    /* Emit the wire-level can_tx event for the UI / scenario log. */
    proto_emit_can_tx(c->id, g_can1.name, frame, c->cycles);

    /* Fan out to peer controllers on the same bus. */
    can_bus_broadcast(&g_can1, frame, &g_can[idx]);
}

static void wire_mcp2515(int idx)
{
    chip_t    *c = &g_chips[idx];
    mcp2515_t *m = &g_can[idx];

    /* Chip id is at most CHIP_ID_MAX-1 chars; ".can1" adds 5 + NUL.
     * Use bounded %.*s so GCC's -Wformat-truncation analyzer can prove
     * the output fits — without the precision specifier it conservatively
     * assumes 279 bytes for an unbounded %s. */
    char id[CHIP_ID_MAX + 8];
    snprintf(id, sizeof(id), "%.*s.can1", (int)(CHIP_ID_MAX - 1), c->id);
    mcp2515_init(m, id);
    m->on_int = on_mcp_int;
    m->on_tx  = on_mcp_tx;
    m->ctx    = (void *)(intptr_t)idx;
    can_bus_attach(&g_can1, m);

    /* SPI: each AVR-master TX byte triggers our model. */
    avr_irq_t *spi_in = avr_io_getirq(c->avr,
                                      AVR_IOCTL_SPI_GETIRQ('0'),
                                      SPI_IRQ_INPUT);
    if (spi_in) {
        avr_irq_register_notify(spi_in, on_mcp_spi_byte,
                                (void *)(intptr_t)idx);
    }

    /* CS: edge changes drive transaction boundaries. */
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

/* ----- I2C LCD <-> simavr glue ----------------------------------------
 *
 * The PCF8574-backed 1602 module sits at 0x27 on each ECU's TWI bus.
 * simavr's TWI delivers START / STOP / WRITE / READ events one byte at
 * a time via a single IRQ value carrying msg/addr/data fields. We:
 *
 *   - on START whose 7-bit address matches LCD_I2C_ADDR, mark this LCD
 *     as the selected slave and raise an ACK back at the master;
 *   - while selected, push every WRITE byte into i2c_lcd_write_byte and
 *     ACK each one;
 *   - on STOP, drop the selection;
 *   - reads are NACKed (the backpack is write-only in practice).
 *
 * To raise IRQs back at the AVR TWI master we allocate a 2-entry IRQ
 * pool per LCD (the same convention simavr's i2c_eeprom example uses)
 * and connect those to the AVR's TWI IRQs.
 */
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

    /* The "selected" flag piggy-backs on the LCD struct via the unused
     * bits of last_byte? No — give it a small dedicated tracker. We
     * store it in a static array indexed by chip. */
    static bool lcd_selected[4] = {false, false, false, false};

    if (v.u.twi.msg & TWI_COND_STOP) {
        lcd_selected[idx] = false;
    }
    if (v.u.twi.msg & TWI_COND_START) {
        /* Address byte: top 7 bits = slave address, LSB = R/W. */
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
    /* Reads from the backpack are not meaningful for our model; leave
     * them unanswered (the master will NACK after timeout). */
}

static const char *_lcd_irq_names[2] = {
    [TWI_IRQ_INPUT]  = "8>lcd.tx",   /* slave -> AVR (ACK / read data) */
    [TWI_IRQ_OUTPUT] = "32<lcd.rx",  /* AVR  -> slave (START/STOP/data) */
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

    /* Allocate this slave's IRQ pair and connect to the AVR's TWI. */
    avr_irq_t *irq = avr_alloc_irq(&c->avr->irq_pool, 0, 2, _lcd_irq_names);
    g_lcd_irq[idx] = irq;
    avr_irq_register_notify(irq + TWI_IRQ_OUTPUT, on_twi_msg,
                            (void *)(intptr_t)idx);

    /* The atmega328p's TWI peripheral is registered with name=0 (the
     * default in megax8.h's struct init), NOT '0' (0x30). Look it up
     * with both forms to be robust to either. */
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
    /* GPIO: watch PORTB / PORTC / PORTD pins 0..7 */
    static const char ports[] = {'B', 'C', 'D'};
    for (size_t p = 0; p < sizeof(ports); p++) {
        char port = ports[p];
        for (int bit = 0; bit < 8; bit++) {
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

    /* UART0 output: TX byte stream from firmware. Disable simavr's stdio
     * cooked-mode (which would buffer until newline / pause on no listener). */
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

    /* PWM outputs (LDR_LED, LOOP, dimmable DOUT, etc.). */
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
        wire_chip_irqs(&g_chips[i]);
        g_nchips++;
    }
    /* Single shared CAN bus, attach all four ECUs. Must come before
     * wire_mcp2515 so that on_tx -> can_bus_broadcast has a target. */
    can_bus_init(&g_can1, "can1");

    /* Attach an MCP2515 to each ECU (indices 0..3). The MCU at index 4
     * has no CAN controller. */
    for (int i = 0; i < 4; i++) {
        wire_mcp2515(i);
    }
    /* Attach a 1602 I2C LCD to each ECU's TWI bus. The MCU has no LCD. */
    for (int i = 0; i < 4; i++) {
        wire_lcd(i);
    }
    proto_emit_log("info",
                   "sim_loop: initialized %d chips, 4 mcp2515, 4 lcd, bus '%s'",
                   g_nchips, g_can1.name);
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

int sim_loop_count(void)
{
    return g_nchips;
}

struct can_bus *sim_loop_bus(void)
{
    return &g_can1;
}

bool sim_loop_tick(void)
{
    if (atomic_load(&g_paused_all)) return true;

    int active = 0;
    for (int i = 0; i < g_nchips; i++) {
        chip_t *c = &g_chips[i];
        if (!c->running || c->paused) continue;
        active++;
        for (int n = 0; n < SIM_CYCLES_PER_TICK; n++) {
            int s = chip_step(c);
            if (s == cpu_Done || s == cpu_Crashed) {
                c->running = false;
                proto_emit_log("warn", "chip %s: stopped (state=%d)", c->id, s);
                break;
            }
        }
    }

    /* TODO(M5): wallclock pacing when g_speed != 0. For M1 we run flat-out. */
    (void)g_speed;

    return active > 0;
}

void sim_loop_pause_all(void)   { atomic_store(&g_paused_all, true); }
void sim_loop_resume_all(void)  { atomic_store(&g_paused_all, false); }
void sim_loop_set_speed(double factor) { g_speed = factor; }
void sim_loop_request_stop(void){ atomic_store(&g_stop_req, true); }
bool sim_loop_should_stop(void) { return atomic_load(&g_stop_req); }

void sim_loop_shutdown(void)
{
    for (int i = 0; i < g_nchips; i++) chip_free(&g_chips[i]);
    g_nchips = 0;
}
