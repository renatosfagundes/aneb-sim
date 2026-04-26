#include "can_bus.h"

#include <string.h>

void can_bus_init(can_bus_t *bus, const char *name)
{
    memset(bus, 0, sizeof(*bus));
    if (name) {
        size_t n = strlen(name);
        if (n >= sizeof(bus->name)) n = sizeof(bus->name) - 1;
        memcpy(bus->name, name, n);
    }
}

int can_bus_attach(can_bus_t *bus, mcp2515_t *node)
{
    if (!bus || !node) return -1;

    /* Reject double-attach. */
    for (int i = 0; i < bus->num_nodes; i++) {
        if (bus->nodes[i] == node) return -1;
    }

    if (bus->num_nodes >= CAN_BUS_MAX_NODES) return -1;

    int idx = bus->num_nodes;
    bus->nodes[idx] = node;
    bus->num_nodes++;
    return idx;
}

void can_bus_broadcast(can_bus_t *bus,
                       const mcp2515_frame_t *frame,
                       mcp2515_t *source)
{
    if (!bus || !frame) return;

    bus->frames_broadcast++;
    for (int i = 0; i < bus->num_nodes; i++) {
        mcp2515_t *n = bus->nodes[i];
        if (!n || n == source) continue;
        if (mcp2515_rx_frame(n, frame)) {
            bus->frames_delivered++;
        }
    }
}

void can_bus_inject(can_bus_t *bus, const mcp2515_frame_t *frame)
{
    if (!bus || !frame) return;
    bus->frames_injected++;
    can_bus_broadcast(bus, frame, NULL);
}
