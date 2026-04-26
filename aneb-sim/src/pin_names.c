#include "pin_names.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int pin_parse(const char *name, char *out_port, int *out_bit)
{
    if (!name || !out_port || !out_bit) return -1;
    if (strlen(name) != 3) return -2;
    if (toupper((unsigned char)name[0]) != 'P') return -3;

    char port = (char)toupper((unsigned char)name[1]);
    if (port < 'B' || port > 'D') return -4;

    if (!isdigit((unsigned char)name[2])) return -5;
    int bit = name[2] - '0';
    if (bit < 0 || bit > 7) return -6;

    *out_port = port;
    *out_bit  = bit;
    return 0;
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
