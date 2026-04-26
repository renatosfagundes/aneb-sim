/*
 * sim_loop.h — multi-chip lockstep scheduler.
 *
 * Owns the global chip table. The main loop calls sim_loop_tick() repeatedly;
 * each tick advances every running chip by SIM_CYCLES_PER_TICK simavr cycles.
 *
 * Single-threaded with respect to the chips. Commands arrive via cmd.c
 * which mutates chip state between ticks.
 */
#ifndef ANEB_SIM_LOOP_H
#define ANEB_SIM_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "chip.h"

#define SIM_MAX_CHIPS         5
#define SIM_CYCLES_PER_TICK   1000   /* ~62.5 us at 16 MHz */

/*
 * Initialize the chip table with the canonical ANEB roster:
 *   ecu1..ecu4 = atmega328p
 *   mcu        = atmega328pb
 *
 * Hooks GPIO (PORTB/C/D) and UART0 IRQs on each chip so transitions
 * become protocol events.
 *
 * Returns 0 on success.
 */
int sim_loop_init(void);

/*
 * Look up a chip by id ("ecu1", "mcu", ...). Returns NULL if not found.
 */
chip_t *sim_loop_find(const char *id);

/*
 * Iterate over all chips. Caller MUST NOT free or replace any chip.
 */
chip_t *sim_loop_chip(int index);
int     sim_loop_count(void);

/*
 * Access the shared CAN bus (CAN1). Used by command dispatch to inject
 * frames from the UI / scenario player. Returns NULL before init.
 */
struct can_bus;
struct can_bus *sim_loop_bus(void);

/*
 * Advance the simulation by one tick (SIM_CYCLES_PER_TICK cycles per chip).
 * Returns false if all chips have stopped (cpu_Done / cpu_Crashed) and there
 * is no further work — main loop should exit.
 */
bool sim_loop_tick(void);

/*
 * Execution control.
 */
void sim_loop_pause_all(void);
void sim_loop_resume_all(void);
void sim_loop_set_speed(double factor);  /* 1.0 = real time; <1.0 = slowed */
void sim_loop_request_stop(void);
bool sim_loop_should_stop(void);

void sim_loop_shutdown(void);

#endif
