/*
 * chip.h — one simulated AVR chip in the ANEB sim.
 *
 * Five instances exist at runtime: ecu1..ecu4 (atmega328p) and mcu
 * (atmega328pb, the board controller).
 *
 * Each chip owns its avr_lock mutex; the per-chip simulation thread holds
 * it during avr_run() and any IRQ raise on that chip's avr_t must also be
 * made under that lock (from whatever thread initiates it).
 */
#ifndef ANEB_CHIP_H
#define ANEB_CHIP_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "sim_avr.h"

#define CHIP_ID_MAX 16

typedef struct chip {
    char     id[CHIP_ID_MAX];   /* "ecu1".."ecu4", "mcu" */
    char     mcu_name[16];      /* "atmega328p" / "atmega328pb" */
    avr_t   *avr;
    uint64_t cycles;            /* total executed (monotonic) */
    bool     running;           /* false until firmware loaded + initialized */
    bool     paused;            /* manual pause via cmd */

    /* Threading: held during avr_run() and any external avr_raise_irq(). */
    pthread_mutex_t avr_lock;

    /* Per-chip real-time pacing state (managed by sim_loop.c). */
    struct timespec pace_wall0;
    uint64_t        pace_sim0;
    bool            pace_init;
} chip_t;

/*
 * Initialize a chip with the given id and mcu_name. Allocates and inits
 * the simavr core but does NOT load firmware — call chip_load_hex() for that.
 * Returns 0 on success, negative on error.
 */
int chip_init(chip_t *c, const char *id, const char *mcu_name);

/*
 * Load an Intel-hex firmware into the chip's flash and ready it to run.
 * Sets c->running = true on success. Replacing firmware on a running chip
 * is allowed (acts as load + reset).
 * Returns 0 on success, negative on error.
 */
int chip_load_hex(chip_t *c, const char *path);

/*
 * Hard-reset the chip (clears RAM, restarts execution from the reset
 * vector). Firmware in flash is preserved.
 */
void chip_reset(chip_t *c);

/*
 * Run one cycle on the chip. Returns the simavr cpu_state value
 * (cpu_Running / cpu_Done / cpu_Crashed / cpu_Sleeping).
 */
int  chip_step(chip_t *c);

/*
 * Tear down. Frees the simavr core.
 */
void chip_free(chip_t *c);

#endif
