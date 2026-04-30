/*
 * sim_loop.h — per-chip threaded scheduler.
 *
 * Each chip runs in its own pthread. sim_loop_init() allocates state and
 * wires all simavr IRQs; sim_loop_start() launches the threads.
 *
 * Locking contract:
 *   - avr_run() and every avr_raise_irq() on a chip's avr_t must be made
 *     while holding chip_t.avr_lock (chip thread holds it during the batch;
 *     command dispatch takes it before raising IRQs).
 *   - Incoming CAN frames from peer chips are queued and drained by the
 *     target chip's own thread before each batch (so mcp2515_rx_frame and
 *     its avr_raise_irq also run under avr_lock in the right thread).
 */
#ifndef ANEB_SIM_LOOP_H
#define ANEB_SIM_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "chip.h"
#include "mcp2515.h"

#define SIM_MAX_CHIPS         5
#define SIM_CYCLES_PER_TICK   1000   /* ~62.5 us at 16 MHz */

/*
 * Initialize chip table, wire IRQs.  Must be called before start().
 * Returns 0 on success.
 */
int sim_loop_init(void);

/*
 * Launch one simulation thread per chip.  Call after init() and after any
 * pre-load firmware flags have been applied.
 * Returns 0 on success.
 */
int sim_loop_start(void);

/*
 * Look up a chip by id ("ecu1", "mcu", ...).  Returns NULL if not found.
 */
chip_t *sim_loop_find(const char *id);

/*
 * Iterate over all chips.  Caller MUST NOT free or replace any chip.
 */
chip_t *sim_loop_chip(int index);
int     sim_loop_count(void);

/*
 * Access the shared CAN bus (CAN1).  Used by command dispatch to look up
 * bus metadata (name, counters).  Returns NULL before init.
 */
struct can_bus;
struct can_bus *sim_loop_bus(void);

/*
 * Thread-safe CAN frame injection from the command/UI path.
 * Enqueues the frame into every attached ECU's incoming CAN queue; the
 * chip threads drain and deliver it under their avr_lock.
 */
void sim_loop_can_inject(const mcp2515_frame_t *frame);

/*
 * Execution control (thread-safe).
 */
void sim_loop_pause_all(void);
void sim_loop_resume_all(void);
void sim_loop_set_speed(double factor);  /* 0.0 = flat-out; 1.0 = real-time */
void sim_loop_request_stop(void);
bool sim_loop_should_stop(void);

void sim_loop_shutdown(void);

#endif
