#include "chip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_avr.h"

#include "proto.h"

/* ---------------------------------------------------------------------------
 * Intel-HEX loader
 *
 * Replaces simavr's read_ihex_file(), which has two problems for our case:
 *   1) returns only the FIRST contiguous chunk and silently drops the rest
 *      ("Additional data blocks were ignored");
 *   2) prints "unsupported check type 03" for Optiboot's Start-Segment
 *      record — harmless to the parser itself but combined with (1) it
 *      means Optiboot never loads completely, the chip crashes at reset.
 *
 * We parse all record types 00,01,02,03,04,05 and place every data record
 * directly at its absolute flash address (no chunking in between).
 * ------------------------------------------------------------------------- */

static int ihex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int ihex_byte(const char *s)
{
    int hi = ihex_nibble(s[0]);
    int lo = ihex_nibble(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

static int load_ihex_into_flash(const char *path, uint8_t *flash,
                                uint32_t flash_cap,
                                uint32_t *min_addr_out,
                                uint32_t *max_addr_out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char     line[600];
    uint32_t segment  = 0;
    uint32_t min_addr = UINT32_MAX;
    uint32_t max_addr = 0;
    int      seen_eof = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] != ':') continue;
        size_t L = strlen(line);
        while (L > 0 && (line[L-1] == '\n' || line[L-1] == '\r')) L--;
        if (L < 11) continue;        /* :LLAAAATTCC = 11 chars minimum */

        int count = ihex_byte(line + 1);
        int ah    = ihex_byte(line + 3);
        int al    = ihex_byte(line + 5);
        int rtype = ihex_byte(line + 7);
        if (count < 0 || ah < 0 || al < 0 || rtype < 0) goto bad;
        if (L < (size_t)(9 + count * 2 + 2)) goto bad;

        uint8_t data[256];
        uint8_t cks = (uint8_t)(count + ah + al + rtype);
        for (int i = 0; i < count; i++) {
            int b = ihex_byte(line + 9 + i * 2);
            if (b < 0) goto bad;
            data[i] = (uint8_t)b;
            cks += data[i];
        }
        int file_cks = ihex_byte(line + 9 + count * 2);
        if (file_cks < 0) goto bad;
        cks = (uint8_t)(0x100 - cks);
        if (cks != (uint8_t)file_cks) {
            fprintf(stderr, "load_ihex: %s: bad checksum\n", path);
            goto bad;
        }

        switch (rtype) {
        case 0x00: {
            uint32_t addr = segment + (uint32_t)((ah << 8) | al);
            if (addr + (uint32_t)count > flash_cap) goto bad;
            memcpy(flash + addr, data, (size_t)count);
            if (addr < min_addr) min_addr = addr;
            if (addr + (uint32_t)count > max_addr) max_addr = addr + count;
            break;
        }
        case 0x01:                                   /* end of file */
            seen_eof = 1;
            goto done;
        case 0x02:                                   /* extended segment */
            if (count != 2) goto bad;
            segment = (uint32_t)((data[0] << 8) | data[1]) << 4;
            break;
        case 0x03:                                   /* start segment (CS:IP) */
            break;                                   /* irrelevant for AVR */
        case 0x04:                                   /* extended linear */
            if (count != 2) goto bad;
            segment = (uint32_t)((data[0] << 8) | data[1]) << 16;
            break;
        case 0x05:                                   /* start linear */
            break;                                   /* irrelevant for AVR */
        default:
            break;
        }
    }
done:
    fclose(f);
    if (min_addr == UINT32_MAX) return -1;           /* nothing loaded */
    (void)seen_eof;
    if (min_addr_out) *min_addr_out = min_addr;
    if (max_addr_out) *max_addr_out = max_addr;
    return 0;
bad:
    fclose(f);
    return -1;
}

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

    uint32_t flash_cap = c->avr->flashend + 1;

    /* Stage into a temp buffer so a parse error leaves the chip's flash
     * untouched.  Optiboot images are tiny — no real cost. */
    uint8_t *tmp = (uint8_t *)malloc(flash_cap);
    if (!tmp) return -1;
    memset(tmp, 0xff, flash_cap);

    uint32_t min_a = 0, max_a = 0;
    if (load_ihex_into_flash(path, tmp, flash_cap, &min_a, &max_a) != 0) {
        proto_emit_log("error", "chip %s: failed to read hex '%s'", c->id, path);
        free(tmp);
        return -2;
    }

    memcpy(c->avr->flash, tmp, flash_cap);
    free(tmp);

    /* On reset, jump to the load base.  This emulates the BOOTRST fuse so
     * Optiboot (loaded at 0x7E00 on atmega328p) actually runs after the
     * reset-on-connect that avrdude triggers.  For application firmware
     * loaded at 0 this is a no-op (reset_pc = 0 = the AVR reset vector). */
    c->avr->reset_pc = min_a;
    c->avr->codeend  = c->avr->flashend;
    c->avr->pc       = min_a;
    c->avr->state    = cpu_Running;
    c->running       = true;
    c->paused        = false;
    c->cycles        = 0;
    /* Optiboot checks MCUSR.EXTRF on entry: if not set, it interprets the
     * boot as a warm reset and JMPs to the application at 0x0000.  With
     * empty application flash that path walks 0xFF instructions until it
     * falls off flashend and simavr crashes the chip.  Setting EXTRF
     * makes Optiboot enter its STK500 sync-wait loop instead.  See
     * MCUSR=I/O 0x34 → data[0x54], EXTRF = bit 1, on atmega328p. */
    c->avr->data[0x54] = 0x02;

    proto_emit_log("info",
                   "chip %s: loaded %u bytes from %s (flash 0x%04x-0x%04x)",
                   c->id, max_a - min_a, path, min_a, max_a);
    return 0;
}

void chip_reset(chip_t *c)
{
    if (!c || !c->avr) return;
    avr_reset(c->avr);
    c->avr->state = cpu_Running;
    c->paused = false;
    /* avr_reset clears all I/O regs, including MCUSR.  Re-assert EXTRF so
     * Optiboot (or any bootloader that branches on the reset cause) treats
     * this as the external-reset that avrdude's DTR pulse simulates,
     * rather than a warm reset of unknown cause. */
    c->avr->data[0x54] = 0x02;
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
