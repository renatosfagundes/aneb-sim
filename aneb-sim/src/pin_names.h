/*
 * pin_names.h — parse port-form GPIO names ("PB5", "PD2", ...).
 *
 * M1 supports only port-form names. Arduino digital ("D7") and analog
 * ("A4") aliases are added in M5 alongside the board's semantic GPIO map.
 */
#ifndef ANEB_PIN_NAMES_H
#define ANEB_PIN_NAMES_H

/*
 * Parse a pin name like "PB5" or "PD2" into the port letter and bit index.
 * Returns 0 on success and sets *out_port to 'B'/'C'/'D' and *out_bit to 0..7.
 * Returns negative on parse error.
 */
int pin_parse(const char *name, char *out_port, int *out_bit);

/*
 * Format a port + bit back into a static buffer (NOT thread-safe; use
 * only from the simulation thread, which is the sole emitter of pin events).
 */
const char *pin_format(char port, int bit);

#endif
