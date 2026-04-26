/*
 * proto.h — JSON-Lines wire protocol between aneb-sim engine and UI.
 *
 * Every message is a single-line JSON object with a version envelope:
 *     {"v":1,"t":"pin","chip":"ecu1","pin":"PB5","val":1,"ts":12345}
 *     {"v":1,"c":"din","chip":"ecu1","pin":"PD2","val":1}
 *
 * `t` indicates engine-emitted EVENT type; `c` indicates UI COMMAND type.
 * `ts` is the simulation time in cycles of the originating chip when the
 * event was generated (engine-side wallclock is not exposed).
 */
#ifndef ANEB_PROTO_H
#define ANEB_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANEB_PROTO_VERSION 1

/* Forward declarations from mcp2515.h to avoid pulling that whole header
 * in here (proto.h is included widely; mcp2515.h is module-local). */
struct mcp2515_frame;

/* ----- Event emission (engine -> UI) ------------------------------------ */

void proto_emit_pin     (const char *chip, const char *pin_name, int val, uint64_t ts);
void proto_emit_pwm     (const char *chip, const char *pin_name, double duty, uint64_t ts);
void proto_emit_uart    (const char *chip, const uint8_t *data, size_t len, uint64_t ts);
void proto_emit_can_tx  (const char *src_chip, const char *bus,
                         const struct mcp2515_frame *f, uint64_t ts);
void proto_emit_can_state(const char *chip, uint8_t tec, uint8_t rec,
                          const char *state, uint64_t ts);
void proto_emit_log     (const char *level, const char *fmt, ...);

/* ----- Command parsing (UI -> engine) ----------------------------------- */

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_DIN,         /* set digital-input pin level */
    CMD_ADC,         /* set ADC channel value (0..1023) */
    CMD_UART,        /* push bytes into a chip's UART RX */
    CMD_LOAD,        /* load a hex file into a chip */
    CMD_RESET,       /* hard-reset a chip */
    CMD_SPEED,       /* wallclock multiplier (>0) */
    CMD_PAUSE,
    CMD_RESUME,
    CMD_STEP,         /* run N cycles, then auto-pause */
    CMD_CAN_INJECT,   /* inject a CAN frame onto the bus */
    CMD_FORCE_BUSOFF, /* drive an ECU's MCP2515 directly into bus-off */
    CMD_CAN_ERRORS,   /* inject N tx-or-rx error increments on a chip */
    CMD_CAN_RECOVER,  /* clear bus-off + counters on a chip */
} cmd_type_t;

typedef struct {
    cmd_type_t  type;
    char        chip[16];   /* "ecu1".."ecu4", "mcu", or empty for global */
    char        pin[8];     /* "PB5", "PD2", ... (CMD_DIN) */
    int         channel;    /* ADC channel index 0..7 (CMD_ADC) */
    int         val;        /* digital level (CMD_DIN), ADC value (CMD_ADC) */
    char        path[256];  /* firmware path (CMD_LOAD) */
    char       *data;       /* malloc'd UART/CAN payload — caller frees */
    size_t      data_len;
    double      speed;      /* CMD_SPEED multiplier */
    uint64_t    cycles;     /* CMD_STEP cycle count */

    /* CAN-frame fields (CMD_CAN_INJECT). */
    char        bus[16];    /* "can1" */
    uint32_t    can_id;
    bool        can_ext;
    bool        can_rtr;
    uint8_t     can_dlc;

    /* Error-injection fields (CMD_CAN_ERRORS). */
    int         err_tx;     /* TX error count to inject */
    int         err_rx;     /* RX error count to inject */
} cmd_t;

/*
 * Parse one JSON-Lines command into *out.
 * Returns 0 on success, negative on parse error.
 * On success, caller owns out->data and must free() it if non-NULL.
 */
int  proto_parse_command(const char *line, cmd_t *out);
void proto_free_command (cmd_t *cmd);

#endif /* ANEB_PROTO_H */
