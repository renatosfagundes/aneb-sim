/*
 * pin_names.h — pin-name parsing.
 *
 * Three accepted forms in commands like `din` and event payloads:
 *
 *   1. Port form, e.g. "PB5" / "PD2" — direct port + bit on the AVR.
 *   2. Arduino digital form, e.g. "D0".."D13" — the Arduino Uno / Nano
 *      digital pin numbering, mapped to the underlying port + bit.
 *   3. Arduino analog-as-digital form, e.g. "A0".."A5" — A0..A5 alias to
 *      PC0..PC5 for digital I/O. (A6 and A7 are ADC-only and have no
 *      digital path on the ATmega328P.)
 *
 * Events emitted by the engine always use the canonical port form
 * ("PB5"). The aliasing is for inbound commands and for the ADC channel
 * lookup.
 */
#ifndef ANEB_PIN_NAMES_H
#define ANEB_PIN_NAMES_H

/*
 * Parse a digital-pin name into port + bit.
 *
 * Accepted forms (case-insensitive on the leading letter):
 *   "PB5", "PD2", "PC4"  - port form
 *   "D0".."D13"          - Arduino digital
 *   "A0".."A5"           - Arduino analog used as digital
 *
 * Returns 0 on success and writes 'B'/'C'/'D' + 0..7. Returns negative
 * for malformed input or for ADC-only aliases (A6/A7) which have no
 * digital wiring.
 */
int pin_parse(const char *name, char *out_port, int *out_bit);

/*
 * Format a port + bit as the canonical port-form string ("PB5").
 * Buffer is static — NOT thread-safe; only called from the simulation
 * thread, the sole emitter of pin events.
 */
const char *pin_format(char port, int bit);

/*
 * Parse an ADC-channel reference. Accepts:
 *   "A0".."A7"     - Arduino analog channel
 *   "0".."7"       - bare decimal channel index
 *
 * Returns the channel 0..7 on success, -1 on error.
 */
int adc_channel_parse(const char *name);

#endif
