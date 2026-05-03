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

    /* Firmware metadata for the UI's chip-info sidebar.  Populated by
     * chip_load_hex (when the user loads through aneb-sim's own UI),
     * and tagged "(via avrdude)" by sim_loop on flash_done when the
     * firmware came in over the STK500 TCP path instead.  Empty until
     * something has been loaded. */
    char     hex_path[260];
    char     hex_name[64];

    /* Cycle of the last chip-stats JSON emit; sim_loop ticks this at
     * ~1 Hz so the UI gets a periodic refresh of free-RAM and the
     * hex-name fields above without flooding the protocol channel. */
    uint64_t last_stat_emit_cycles;

    /* Threading: held during avr_run() and any external avr_raise_irq(). */
    pthread_mutex_t avr_lock;

    /* Per-chip real-time pacing state (managed by sim_loop.c). */
    struct timespec pace_wall0;
    uint64_t        pace_sim0;
    bool            pace_init;

    /* UART RX pacing: next AVR cycle at which a TCP-sourced byte may be
     * pushed into the chip's UART.  Throttles delivery to the simavr
     * UART byte rate so its 64-byte input FIFO can't be overrun by a
     * burst send (e.g. avrdude's 133-byte STK_PROG_PAGE in one TCP
     * write).  Updated by sim_loop.c. */
    uint64_t        uart_rx_due_cycle;
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
 * vector). Firmware in flash is preserved.  Sets MCUSR.EXTRF=1 so
 * Optiboot enters its sync-wait loop (avrdude DTR-pulse semantics).
 */
void chip_reset(chip_t *c);

/*
 * Reset like chip_reset() but with MCUSR=0 — no EXTRF, no WDRF.
 * Optiboot's bootloader-vs-app branch reads MCUSR and skips the sync-wait
 * loop when EXTRF is clear, jumping straight to the application at 0x0000.
 * Used after a flasher client (avrdude) disconnects to deterministically
 * transition into the freshly programmed user firmware.
 */
void chip_soft_reset(chip_t *c);

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
