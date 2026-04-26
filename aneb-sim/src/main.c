/*
 * aneb-sim — M0 smoke test
 *
 * Loads a single Intel-hex firmware into one ATmega328P core and prints
 * every GPIO pin transition on PORTB / PORTC / PORTD. No CAN, no UI, no
 * peripherals beyond GPIO — just enough to verify simavr is wired correctly
 * under MSYS2.
 *
 * Replaced in M1 by the full multi-chip JSON engine.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "sim_avr.h"
#include "sim_hex.h"
#include "avr_ioport.h"

static avr_t        *g_avr  = NULL;
static volatile int  g_done = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_done = 1;
}

static void on_pin_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    fprintf(stdout, "PIN %s = %u\n", (const char *)param, value);
    fflush(stdout);
}

static void watch_port(avr_t *avr, char port_letter, const char *names[8])
{
    for (int i = 0; i < 8; i++) {
        avr_irq_t *irq = avr_io_getirq(avr,
                                       AVR_IOCTL_IOPORT_GETIRQ(port_letter),
                                       i);
        if (irq) {
            avr_irq_register_notify(irq, on_pin_change, (void *)names[i]);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <firmware.hex>\n", argv[0]);
        return 2;
    }
    const char *hex_path = argv[1];

    g_avr = avr_make_mcu_by_name("atmega328p");
    if (!g_avr) {
        fprintf(stderr, "error: could not create atmega328p core\n");
        return 1;
    }
    avr_init(g_avr);
    g_avr->frequency = 16000000UL;

    uint32_t base = 0, size = 0;
    uint8_t *blob = read_ihex_file(hex_path, &size, &base);
    if (!blob || size == 0) {
        fprintf(stderr, "error: failed to read hex: %s\n", hex_path);
        return 1;
    }
    if (base + size > g_avr->flashend + 1) {
        fprintf(stderr, "error: hex too large for flash (size=%u base=0x%x)\n",
                size, base);
        free(blob);
        return 1;
    }
    memcpy(g_avr->flash + base, blob, size);
    free(blob);
    g_avr->codeend = g_avr->flashend;
    g_avr->pc      = base;

    static const char *port_b[8] = {"PB0","PB1","PB2","PB3","PB4","PB5","PB6","PB7"};
    static const char *port_c[8] = {"PC0","PC1","PC2","PC3","PC4","PC5","PC6","PC7"};
    static const char *port_d[8] = {"PD0","PD1","PD2","PD3","PD4","PD5","PD6","PD7"};
    watch_port(g_avr, 'B', port_b);
    watch_port(g_avr, 'C', port_c);
    watch_port(g_avr, 'D', port_d);

    signal(SIGINT, on_sigint);

    fprintf(stderr, "aneb-sim smoke test: running %s\n", hex_path);
    fprintf(stderr, "Watching PORTB, PORTC, PORTD. Ctrl-C to stop.\n");

    while (!g_done) {
        int state = avr_run(g_avr);
        if (state == cpu_Done || state == cpu_Crashed) {
            fprintf(stderr, "core stopped (state=%d)\n", state);
            break;
        }
    }
    return 0;
}
