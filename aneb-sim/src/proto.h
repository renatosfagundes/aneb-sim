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

#include <stddef.h>
#include <stdint.h>

#define ANEB_PROTO_VERSION 1

/* ----- Event emission (engine -> UI) ------------------------------------ */

void proto_emit_pin (const char *chip, const char *pin_name, int val, uint64_t ts);
void proto_emit_pwm (const char *chip, const char *pin_name, double duty, uint64_t ts);
void proto_emit_uart(const char *chip, const uint8_t *data, size_t len, uint64_t ts);
void proto_emit_log (const char *level, const char *fmt, ...);

/* ----- Command parsing (UI -> engine) ----------------------------------- */

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_DIN,        /* set digital-input pin level */
    CMD_ADC,        /* set ADC channel value (0..1023) */
    CMD_UART,       /* push bytes into a chip's UART RX */
    CMD_LOAD,       /* load a hex file into a chip */
    CMD_RESET,      /* hard-reset a chip */
    CMD_SPEED,      /* wallclock multiplier (>0) */
    CMD_PAUSE,
    CMD_RESUME,
    CMD_STEP,       /* run N cycles, then auto-pause */
} cmd_type_t;

typedef struct {
    cmd_type_t  type;
    char        chip[16];   /* "ecu1".."ecu4", "mcu", or empty for global */
    char        pin[8];     /* "PB5", "PD2", ... (CMD_DIN) */
    int         channel;    /* ADC channel index 0..7 (CMD_ADC) */
    int         val;        /* digital level (CMD_DIN), ADC value (CMD_ADC) */
    char        path[256];  /* firmware path (CMD_LOAD) */
    char       *data;       /* malloc'd UART payload (CMD_UART) — caller frees */
    size_t      data_len;
    double      speed;      /* CMD_SPEED multiplier */
    uint64_t    cycles;     /* CMD_STEP cycle count */
} cmd_t;

/*
 * Parse one JSON-Lines command into *out.
 * Returns 0 on success, negative on parse error.
 * On success, caller owns out->data and must free() it if non-NULL.
 */
int  proto_parse_command(const char *line, cmd_t *out);
void proto_free_command (cmd_t *cmd);

#endif /* ANEB_PROTO_H */
