#include "chip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_avr.h"
#include "sim_hex.h"

#include "proto.h"

int chip_init(chip_t *c, const char *id, const char *mcu_name)
{
    if (!c || !id || !mcu_name) return -1;
    memset(c, 0, sizeof(*c));

    strncpy(c->id,       id,       CHIP_ID_MAX - 1);
    strncpy(c->mcu_name, mcu_name, sizeof(c->mcu_name) - 1);

    c->avr = avr_make_mcu_by_name(mcu_name);
    if (!c->avr) {
        proto_emit_log("error", "chip %s: unknown mcu '%s'", id, mcu_name);
        return -2;
    }
    avr_init(c->avr);
    c->avr->frequency = 16000000UL;

    pthread_mutex_init(&c->avr_lock, NULL);
    c->pace_init = false;
    c->running = false;
    c->paused  = false;
    c->cycles  = 0;
    return 0;
}

int chip_load_hex(chip_t *c, const char *path)
{
    if (!c || !c->avr || !path) return -1;

    uint32_t base = 0, size = 0;
    uint8_t *blob = read_ihex_file(path, &size, &base);
    if (!blob || size == 0) {
        proto_emit_log("error", "chip %s: failed to read hex '%s'", c->id, path);
        return -2;
    }
    if (base + size > c->avr->flashend + 1) {
        proto_emit_log("error",
                       "chip %s: hex too large (size=%u base=0x%x)",
                       c->id, size, base);
        free(blob);
        return -3;
    }

    /* Clear program flash, then load. */
    memset(c->avr->flash, 0xff, c->avr->flashend + 1);
    memcpy(c->avr->flash + base, blob, size);
    free(blob);

    c->avr->codeend = c->avr->flashend;
    c->avr->pc      = base;
    c->avr->state   = cpu_Running;
    c->running      = true;
    c->paused       = false;
    c->cycles       = 0;

    proto_emit_log("info", "chip %s: loaded %u bytes from %s",
                   c->id, size, path);
    return 0;
}

void chip_reset(chip_t *c)
{
    if (!c || !c->avr) return;
    avr_reset(c->avr);
    c->avr->state = cpu_Running;
    c->paused = false;
    proto_emit_log("info", "chip %s: reset", c->id);
}

int chip_step(chip_t *c)
{
    if (!c || !c->avr || !c->running || c->paused) return cpu_Sleeping;
    int s = avr_run(c->avr);
    c->cycles++;
    return s;
}

void chip_free(chip_t *c)
{
    if (!c) return;
    if (c->avr) {
        avr_terminate(c->avr);
        c->avr = NULL;
    }
    pthread_mutex_destroy(&c->avr_lock);
    c->running = false;
}
