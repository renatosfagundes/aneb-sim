#include "chip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_avr.h"

#include "proto.h"
#include "optiboot_data.h"

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

/* Parse one Intel-hex record at `line` (length L, no trailing newline).
 * Updates *segment, *min_addr, *max_addr; writes data records to flash.
 * Returns 0 to keep going, 1 on EOF record (rtype 0x01), -1 on parse error. */
static int parse_ihex_record(const char *line, size_t L,
                             uint8_t *flash, uint32_t flash_cap,
                             uint32_t *segment,
                             uint32_t *min_addr, uint32_t *max_addr,
                             const char *src_label)
{
    if (L < 11) return 0;                             /* skip junk lines */
    if (line[0] != ':') return 0;

    int count = ihex_byte(line + 1);
    int ah    = ihex_byte(line + 3);
    int al    = ihex_byte(line + 5);
    int rtype = ihex_byte(line + 7);
    if (count < 0 || ah < 0 || al < 0 || rtype < 0) return -1;
    if (L < (size_t)(9 + count * 2 + 2)) return -1;

    uint8_t data[256];
    uint8_t cks = (uint8_t)(count + ah + al + rtype);
    for (int i = 0; i < count; i++) {
        int b = ihex_byte(line + 9 + i * 2);
        if (b < 0) return -1;
        data[i] = (uint8_t)b;
        cks += data[i];
    }
    int file_cks = ihex_byte(line + 9 + count * 2);
    if (file_cks < 0) return -1;
    cks = (uint8_t)(0x100 - cks);
    if (cks != (uint8_t)file_cks) {
        fprintf(stderr, "load_ihex: %s: bad checksum\n",
                src_label ? src_label : "<buf>");
        return -1;
    }

    switch (rtype) {
    case 0x00: {
        uint32_t addr = *segment + (uint32_t)((ah << 8) | al);
        if (addr + (uint32_t)count > flash_cap) return -1;
        memcpy(flash + addr, data, (size_t)count);
        if (addr < *min_addr) *min_addr = addr;
        if (addr + (uint32_t)count > *max_addr) *max_addr = addr + count;
        break;
    }
    case 0x01:                                       /* end of file */
        return 1;
    case 0x02:                                       /* extended segment */
        if (count != 2) return -1;
        *segment = (uint32_t)((data[0] << 8) | data[1]) << 4;
        break;
    case 0x03:                                       /* start segment (CS:IP) */
        break;                                       /* irrelevant for AVR */
    case 0x04:                                       /* extended linear */
        if (count != 2) return -1;
        *segment = (uint32_t)((data[0] << 8) | data[1]) << 16;
        break;
    case 0x05:                                       /* start linear */
        break;                                       /* irrelevant for AVR */
    default:
        break;
    }
    return 0;
}

/* Iterate `text` line-by-line, parsing Intel-hex records into `flash`. */
static int load_ihex_text_into_flash(const char *text, uint8_t *flash,
                                     uint32_t flash_cap,
                                     uint32_t *min_addr_out,
                                     uint32_t *max_addr_out,
                                     const char *src_label)
{
    uint32_t segment  = 0;
    uint32_t min_addr = UINT32_MAX;
    uint32_t max_addr = 0;

    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t L = eol ? (size_t)(eol - p) : strlen(p);
        /* trim trailing CR (DOS line endings) */
        while (L > 0 && p[L - 1] == '\r') L--;

        int r = parse_ihex_record(p, L, flash, flash_cap,
                                  &segment, &min_addr, &max_addr, src_label);
        if (r < 0) return -1;
        if (r > 0) break;                            /* EOF record */

        if (!eol) break;
        p = eol + 1;
    }
    if (min_addr == UINT32_MAX) return -1;           /* nothing loaded */
    if (min_addr_out) *min_addr_out = min_addr;
    if (max_addr_out) *max_addr_out = max_addr;
    return 0;
}

static int load_ihex_into_flash(const char *path, uint8_t *flash,
                                uint32_t flash_cap,
                                uint32_t *min_addr_out,
                                uint32_t *max_addr_out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    /* Read whole file into memory and parse it as a string.  Hex files
     * are tiny (Optiboot ≈ 1.5 KB; user sketches a few tens of KB), so
     * the memory cost is negligible and lets us share parser logic with
     * the embedded-Optiboot path. */
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);

    int r = load_ihex_text_into_flash(buf, flash, flash_cap,
                                      min_addr_out, max_addr_out, path);
    free(buf);
    return r;
}

/* Write `rjmp .` (encoded 0xCFFF, little-endian bytes 0xFF 0xCF) at the
 * application reset vector at 0x0000 of `flash`.  This gives Optiboot's
 * sync-timeout fall-through somewhere safe to land when the chip has no
 * user firmware yet — without it, the AVR walks 0xFF instructions
 * through empty flash, eventually overruns `flashend`, and simavr trips
 * avr_sadly_crashed.  The stub is overwritten the moment any sketch
 * is flashed (its reset vector occupies the same bytes). */
static void install_idle_stub(uint8_t *flash)
{
    flash[0x0000] = 0xFF;
    flash[0x0001] = 0xCF;
}

/* Load the embedded Optiboot bootloader into the chip's flash and set
 * reset_pc to its base, so a hard reset always lands in the bootloader.
 * Returns 0 on success, negative if the embedded image fails to parse
 * (which would be a build problem, not a runtime one). */
static int chip_install_optiboot(chip_t *c)
{
    if (!c || !c->avr) return -1;

    uint32_t flash_cap = c->avr->flashend + 1;
    uint32_t min_a = 0, max_a = 0;
    int r = load_ihex_text_into_flash(OPTIBOOT_ATMEGA328P_HEX,
                                       c->avr->flash, flash_cap,
                                       &min_a, &max_a, "<embedded optiboot>");
    if (r != 0) {
        proto_emit_log("error",
                       "chip %s: embedded Optiboot failed to load", c->id);
        return -1;
    }

    /* Halt-stub at 0x0000 so the empty-app fall-through doesn't crash. */
    install_idle_stub(c->avr->flash);

    /* Emulate BOOTRST: reset jumps to the bootloader, not address 0. */
    c->avr->reset_pc = min_a;
    c->avr->codeend  = c->avr->flashend;
    c->avr->pc       = min_a;
    return 0;
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
    c->paused   = false;
    c->cycles   = 0;
    c->hex_path[0] = '\0';
    snprintf(c->hex_name, sizeof(c->hex_name), "%s",
             "Optiboot (built-in)");
    c->last_stat_emit_cycles = 0;

    /* Pre-install Optiboot on every atmega328p chip so external tools
     * (avrdude over the TCP UART, the aneb-sim UI's flash button, the
     * Arduino IDE itself) always see a functioning bootloader without
     * a manual "load Optiboot" bootstrap step.  Sketches loaded later
     * via chip_load_hex layer on top of Optiboot — the application area
     * gets the user's code, the boot section keeps Optiboot intact. */
    if (strncmp(mcu_name, "atmega328p", 10) == 0) {
        if (chip_install_optiboot(c) == 0) {
            /* Optiboot checks MCUSR.EXTRF on entry: if set it enters
             * the STK500 sync-wait loop (ready for avrdude); if not,
             * it jumps to the application at 0x0000.  We set EXTRF so
             * the chip comes up ready to be flashed.  See chip_reset
             * for the same trick on every subsequent reset. */
            c->avr->data[0x54] = 0x02;
            c->avr->state = cpu_Running;
            c->running    = true;
            proto_emit_log("info",
                           "chip %s: Optiboot pre-loaded, running", c->id);
        } else {
            c->running = false;
        }
    } else {
        /* No Optiboot for other MCU types (e.g. atmega328pb on the MCU
         * chip): keep legacy behavior — chip waits for chip_load_hex. */
        c->running = false;
    }
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

    /* Pre-load Optiboot for atmega328p chips.  When the user's hex covers
     * the bootloader region (a `with_bootloader` build), the user's
     * bytes overwrite Optiboot below — that's their bootloader and we
     * defer to it.  When the user's hex is application-only, Optiboot
     * is preserved in the boot section and stays ready for avrdude. */
    bool have_opt = false;
    uint32_t opt_min = 0, opt_max = 0;
    if (strncmp(c->mcu_name, "atmega328p", 10) == 0) {
        have_opt = (load_ihex_text_into_flash(OPTIBOOT_ATMEGA328P_HEX,
                                              tmp, flash_cap,
                                              &opt_min, &opt_max,
                                              "<embedded optiboot>") == 0);
        /* Halt-stub at 0x0000 — overwritten if user's hex covers 0x0000
         * (almost always the case), but a safety net otherwise. */
        if (have_opt) install_idle_stub(tmp);
    }

    uint32_t min_a = 0, max_a = 0;
    if (load_ihex_into_flash(path, tmp, flash_cap, &min_a, &max_a) != 0) {
        proto_emit_log("error", "chip %s: failed to read hex '%s'", c->id, path);
        free(tmp);
        return -2;
    }

    memcpy(c->avr->flash, tmp, flash_cap);
    free(tmp);

    /* Reset always jumps to the bootloader (BOOTRST fuse emulation).
     * Optiboot reads MCUSR.EXTRF: if set → STK500 sync-wait, ready
     * for avrdude; if cleared → JMPs to the user app at 0x0000.  With
     * Optiboot pre-loaded this is the right behavior whether the user
     * loaded a sketch with or without an embedded bootloader. */
    uint32_t reset_pc = have_opt ? opt_min : min_a;
    c->avr->reset_pc = reset_pc;
    c->avr->codeend  = c->avr->flashend;
    c->avr->pc       = reset_pc;
    c->avr->state    = cpu_Running;
    c->running       = true;
    c->paused        = false;
    c->cycles        = 0;
    /* Set EXTRF so Optiboot enters sync-wait on the first run after a
     * fresh load — gives the user's avrdude/IDE a window to talk to the
     * bootloader before it falls through to the application. */
    c->avr->data[0x54] = 0x02;

    /* Remember where this came from for the UI's chip-info sidebar.
     * basename = anything after the last '/' or '\' (Windows + POSIX). */
    snprintf(c->hex_path, sizeof(c->hex_path), "%s", path);
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    snprintf(c->hex_name, sizeof(c->hex_name), "%s", base);

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

void chip_soft_reset(chip_t *c)
{
    if (!c || !c->avr) return;
    avr_reset(c->avr);
    c->avr->state = cpu_Running;
    c->paused = false;
    /* MCUSR=0 — no EXTRF and no WDRF.  Reset still lands at reset_pc
     * (Optiboot base), but Optiboot's startup check `if (MCUSR & EXTRF)`
     * is false, so it jumps straight to the user app at 0x0000.  Used
     * after avrdude finishes flashing so the chip drops out of the
     * bootloader's sync-wait loop into the just-programmed sketch
     * without waiting for Optiboot's own watchdog timeout (which depends
     * on simavr's WDT emulation matching the real hardware bit-for-bit). */
    c->avr->data[0x54] = 0x00;
    proto_emit_log("info", "chip %s: soft reset (run app)", c->id);
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
