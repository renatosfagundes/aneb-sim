/*
 * can_bus.h — single-bus, multi-node CAN frame fan-out.
 *
 * The bus owns no AVR or simavr state; it just routes mcp2515_frame_t's
 * between attached MCP2515 controllers. Tests link this directly with
 * the pure mcp2515 module — no simavr required.
 *
 * Scope (M3):
 *   - Up to CAN_BUS_MAX_NODES controllers attached to one bus.
 *   - Broadcast a frame from a source to all OTHER attached nodes.
 *   - External injection (UI / scenario player / unit tests).
 *   - Frame counters for diagnostics.
 *
 * Out of scope until M4:
 *   - Error frames, error injection, TEC/REC accounting.
 *   - Multi-bus topology (the board has only CAN1 in our v1).
 *   - True bit-time arbitration (real CAN: lowest ID wins via dominant
 *     bit physical OR-ing). Our model delivers in lockstep tick order;
 *     same-tick contention is resolved by attach order, not ID.
 */
#ifndef ANEB_CAN_BUS_H
#define ANEB_CAN_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "mcp2515.h"

#define CAN_BUS_MAX_NODES 8
#define CAN_BUS_NAME_MAX  16

typedef struct can_bus {
    char        name[CAN_BUS_NAME_MAX];
    mcp2515_t  *nodes[CAN_BUS_MAX_NODES];
    int         num_nodes;
    uint64_t    frames_broadcast;
    uint64_t    frames_delivered;     /* sum across all receivers */
    uint64_t    frames_injected;      /* via can_bus_inject */
} can_bus_t;

/* Initialize a bus. Safe to call multiple times — state is fully reset. */
void can_bus_init(can_bus_t *bus, const char *name);

/* Attach an MCP2515 controller. Returns the slot index assigned, or -1
 * if the bus is full. Same controller attached twice is a programming
 * error; we return -1 instead of overflowing. */
int  can_bus_attach(can_bus_t *bus, mcp2515_t *node);

/* Broadcast a frame from `source` to all OTHER attached nodes. Each
 * recipient runs its own filter logic; non-matching nodes silently drop.
 * `source` may be NULL for "no source" (equivalent to inject). */
void can_bus_broadcast(can_bus_t *bus,
                       const mcp2515_frame_t *frame,
                       mcp2515_t *source);

/* External injection: deliver to all attached nodes (no source skipped). */
void can_bus_inject(can_bus_t *bus, const mcp2515_frame_t *frame);

#endif
