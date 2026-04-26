#include "pin_names.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Arduino digital-pin number -> (port, bit), per ATmega328P pinout. */
typedef struct { char port; int bit; } port_bit_t;

static const port_bit_t arduino_digital[14] = {
    {'D', 0}, {'D', 1}, {'D', 2}, {'D', 3},   /* D0..D3 */
    {'D', 4}, {'D', 5}, {'D', 6}, {'D', 7},   /* D4..D7 */
    {'B', 0}, {'B', 1}, {'B', 2}, {'B', 3},   /* D8..D11 */
    {'B', 4}, {'B', 5},                       /* D12..D13 */
};

static int parse_port_form(const char *name, char *out_port, int *out_bit)
{
    /* "Pxn" with X in {B,C,D} and n in 0..7 */
    if (strlen(name) != 3) return -1;
    if (toupper((unsigned char)name[0]) != 'P') return -1;
    char port = (char)toupper((unsigned char)name[1]);
    if (port < 'B' || port > 'D') return -1;
    if (!isdigit((unsigned char)name[2])) return -1;
    int bit = name[2] - '0';
    if (bit > 7) return -1;
    *out_port = port;
    *out_bit  = bit;
    return 0;
}

static int parse_arduino_digital(const char *name, char *out_port, int *out_bit)
{
    /* "Dn" with n in 0..13 */
    if (toupper((unsigned char)name[0]) != 'D') return -1;
    if (!name[1]) return -1;
    char *end = NULL;
    long n = strtol(name + 1, &end, 10);
    if (!end || *end != '\0') return -1;
    if (n < 0 || n > 13) return -1;
    *out_port = arduino_digital[n].port;
    *out_bit  = arduino_digital[n].bit;
    return 0;
}

static int parse_arduino_analog_as_digital(const char *name,
                                           char *out_port, int *out_bit)
{
    /* "An" with n in 0..5; A6/A7 have no digital path on 328P. */
    if (toupper((unsigned char)name[0]) != 'A') return -1;
    if (!name[1]) return -1;
    char *end = NULL;
    long n = strtol(name + 1, &end, 10);
    if (!end || *end != '\0') return -1;
    if (n < 0 || n > 5) return -1;       /* 6,7 = ADC-only */
    *out_port = 'C';
    *out_bit  = (int)n;
    return 0;
}

int pin_parse(const char *name, char *out_port, int *out_bit)
{
    if (!name || !out_port || !out_bit) return -1;

    /* Try the three forms in order; the first that matches wins. */
    if (parse_port_form(name, out_port, out_bit) == 0)              return 0;
    if (parse_arduino_digital(name, out_port, out_bit) == 0)        return 0;
    if (parse_arduino_analog_as_digital(name, out_port, out_bit) == 0) return 0;
    return -1;
}

const char *pin_format(char port, int bit)
{
    static char buf[4];
    if (bit < 0 || bit > 7 || port < 'A' || port > 'Z') {
        return "P??";
    }
    snprintf(buf, sizeof(buf), "P%c%d", port, bit);
    return buf;
}

int adc_channel_parse(const char *name)
{
    if (!name || !name[0]) return -1;

    const char *p = name;
    if (toupper((unsigned char)p[0]) == 'A') p++;       /* skip optional 'A' */
    if (!p[0]) return -1;

    char *end = NULL;
    long n = strtol(p, &end, 10);
    if (!end || *end != '\0') return -1;
    if (n < 0 || n > 7) return -1;
    return (int)n;
}
