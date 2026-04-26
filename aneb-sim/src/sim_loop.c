#include "sim_loop.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "avr_uart.h"

#include "chip.h"
#include "pin_names.h"
#include "proto.h"

/* ----- module state ---------------------------------------------------- */

static chip_t            g_chips[SIM_MAX_CHIPS];
static int               g_nchips     = 0;
static atomic_bool       g_stop_req   = false;
static atomic_bool       g_paused_all = false;
static double            g_speed      = 1.0;

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
    proto_emit_log("info", "sim_loop: initialized %d chips", g_nchips);
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
