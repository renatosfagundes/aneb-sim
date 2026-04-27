/*
 * i2c_lcd.c — implementation of the PCF8574/HD44780 model.
 *
 * The HD44780 in 4-bit mode latches one nibble per E falling edge.
 * One full command or data byte is therefore TWO byte-writes to the
 * PCF8574: high nibble first, then low nibble. RS is sampled at the
 * second falling edge (i.e. the byte's RS is whatever RS was on the
 * last completed nibble).
 *
 * The standard library init sequence is:
 *   1) write 0x30 three times in 8-bit mode (function set, 8-bit)
 *   2) write 0x20 once          (function set, switch to 4-bit)
 *   3) from now on, every command/data is two nibbles
 *
 * We mirror that: until we see the 4-bit-mode function-set, each E
 * falling edge is a complete byte (high nibble = full command).
 * Afterwards, nibbles pair up.
 */
#include "i2c_lcd.h"

#include <stdio.h>
#include <string.h>

/* HD44780 instructions we recognise. Anything not listed below is
 * silently ignored — it's still valid for the LCD, just doesn't move
 * any visible state we model. */
#define HD_CLEAR_DISPLAY        0x01
#define HD_RETURN_HOME_MASK     0xFE  /* 0x02 / 0x03 */
#define HD_RETURN_HOME_VALUE    0x02
#define HD_ENTRY_MODE_MASK      0xFC
#define HD_ENTRY_MODE_VALUE     0x04
#define HD_DISPLAY_CTRL_MASK    0xF8
#define HD_DISPLAY_CTRL_VALUE   0x08
#define HD_FUNCTION_SET_MASK    0xE0
#define HD_FUNCTION_SET_VALUE   0x20
#define HD_SET_DDRAM_MASK       0x80

#define HD_DDRAM_ROW1_BASE      0x40

/* ----- helpers ----------------------------------------------------- */

static int ddram_offset(uint8_t addr)
{
    /* Map HD44780 DDRAM address to a (row*COLS+col) offset into our
     * compacted buffer. Returns -1 if the address is outside the two
     * visible rows we model. */
    if (addr <= 0x0F)
        return addr;
    if (addr >= HD_DDRAM_ROW1_BASE && addr <= HD_DDRAM_ROW1_BASE + 0x0F)
        return LCD_COLS + (addr - HD_DDRAM_ROW1_BASE);
    return -1;
}

static void emit_changed(i2c_lcd_t *lcd)
{
    if (!lcd->on_changed) return;
    char line0[LCD_COLS + 1], line1[LCD_COLS + 1];
    i2c_lcd_get_lines(lcd, line0, line1);

    /* Suppress when nothing visible actually changed — avoids
     * flooding the wire with identical lcd events when firmware
     * re-prints unchanged content every loop iteration (e.g. a 25 Hz
     * UI refresh that overwrites the same digits). */
    char snapshot[LCD_ROWS * LCD_COLS];
    memcpy(snapshot,                      line0, LCD_COLS);
    memcpy(snapshot + LCD_COLS,           line1, LCD_COLS);
    if (lcd->have_emitted &&
        memcmp(snapshot, lcd->last_emitted, sizeof(snapshot)) == 0) {
        return;
    }
    memcpy(lcd->last_emitted, snapshot, sizeof(snapshot));
    lcd->have_emitted = true;

    lcd->on_changed(lcd->ctx, line0, line1);
}

static void cursor_advance(i2c_lcd_t *lcd)
{
    if (lcd->auto_increment) {
        lcd->cursor_addr++;
        /* Clamp into the two visible rows; in practice firmware
         * issues setCursor() before overrunning, so simple clamping
         * is enough for our purposes. */
        if (lcd->cursor_addr == 0x10)               lcd->cursor_addr = HD_DDRAM_ROW1_BASE;
        else if (lcd->cursor_addr == HD_DDRAM_ROW1_BASE + 0x10)
                                                    lcd->cursor_addr = 0x00;
    } else {
        if (lcd->cursor_addr == 0x00)               lcd->cursor_addr = HD_DDRAM_ROW1_BASE + 0x0F;
        else if (lcd->cursor_addr == HD_DDRAM_ROW1_BASE)
                                                    lcd->cursor_addr = 0x0F;
        else                                        lcd->cursor_addr--;
    }
}

static void apply_command(i2c_lcd_t *lcd, uint8_t cmd)
{
    if (cmd == HD_CLEAR_DISPLAY) {
        memset(lcd->ddram, ' ', sizeof(lcd->ddram));
        lcd->cursor_addr = 0x00;
        emit_changed(lcd);
        return;
    }
    if ((cmd & HD_RETURN_HOME_MASK) == HD_RETURN_HOME_VALUE) {
        lcd->cursor_addr = 0x00;
        return;
    }
    if ((cmd & HD_ENTRY_MODE_MASK) == HD_ENTRY_MODE_VALUE) {
        lcd->auto_increment = (cmd & 0x02) != 0;
        return;
    }
    if ((cmd & HD_DISPLAY_CTRL_MASK) == HD_DISPLAY_CTRL_VALUE) {
        lcd->display_on = (cmd & 0x04) != 0;
        return;
    }
    if ((cmd & HD_FUNCTION_SET_MASK) == HD_FUNCTION_SET_VALUE) {
        /* Bit 4 (DL) selects 8-bit (1) vs 4-bit (0). Reaching here
         * means we're already in 4-bit mode (the init transition is
         * handled separately) — accept any further function-set
         * writes without changing visible state. */
        return;
    }
    if ((cmd & HD_SET_DDRAM_MASK) == HD_SET_DDRAM_MASK) {
        lcd->cursor_addr = cmd & 0x7F;
        return;
    }
    /* CGRAM addressing (0x40..0x7F sans MSB) and cursor/display shift
     * (0x10..0x1F) fall through unhandled. Custom characters are not
     * modeled. */
}

static void apply_data(i2c_lcd_t *lcd, uint8_t data)
{
    int off = ddram_offset(lcd->cursor_addr);
    if (off >= 0 && off < (int)sizeof(lcd->ddram)) {
        lcd->ddram[off] = data;
    }
    cursor_advance(lcd);
    emit_changed(lcd);
}

/* ----- public API -------------------------------------------------- */

void i2c_lcd_init(i2c_lcd_t *lcd, const char *id, uint8_t i2c_addr)
{
    memset(lcd, 0, sizeof(*lcd));
    if (id) {
        snprintf(lcd->id, sizeof(lcd->id), "%s", id);
    }
    lcd->i2c_addr = i2c_addr;
    i2c_lcd_reset(lcd);
}

void i2c_lcd_reset(i2c_lcd_t *lcd)
{
    /* Display starts blank, cursor at home, in 8-bit-mode init phase
     * waiting for the firmware's standard switch to 4-bit. */
    memset(lcd->ddram, ' ', sizeof(lcd->ddram));
    lcd->cursor_addr    = 0x00;
    lcd->auto_increment = true;     /* HD44780 default after init */
    lcd->display_on     = false;
    lcd->backlight_on   = false;
    lcd->last_byte      = 0;
    lcd->last_e         = false;
    lcd->hi_nibble      = 0;
    lcd->have_hi_nibble = false;
    lcd->in_init_8bit   = true;
    memset(lcd->last_emitted, 0, sizeof(lcd->last_emitted));
    lcd->have_emitted   = false;
}

void i2c_lcd_write_byte(i2c_lcd_t *lcd, uint8_t byte)
{
    /* Track the backlight bit so the UI could mirror it later. */
    lcd->backlight_on = (byte & I2C_LCD_BIT_BL) != 0;

    bool e  = (byte & I2C_LCD_BIT_E) != 0;

    /* HD44780 latches data on the FALLING edge of E. */
    if (lcd->last_e && !e) {
        bool    rs     = (lcd->last_byte & I2C_LCD_BIT_RS) != 0;
        uint8_t nibble = (lcd->last_byte & I2C_LCD_MASK_D) >> 4;

        if (lcd->in_init_8bit) {
            /* Each pulse latches a single nibble that the firmware
             * intends as the high nibble of an 8-bit-style command.
             * The standard sequence is 0x3 (x3) then 0x2 to switch to
             * 4-bit mode; once we see 0x2 with RS=0 we leave init. */
            if (!rs && nibble == 0x2) {
                lcd->in_init_8bit   = false;
                lcd->have_hi_nibble = false;
            }
            /* Other init pulses (0x3 = 8-bit function set) are
             * acknowledged but produce no visible state. */
        } else {
            if (!lcd->have_hi_nibble) {
                lcd->hi_nibble      = nibble;
                lcd->have_hi_nibble = true;
            } else {
                uint8_t full        = (uint8_t)((lcd->hi_nibble << 4) | nibble);
                lcd->have_hi_nibble = false;
                if (rs) apply_data(lcd, full);
                else    apply_command(lcd, full);
            }
        }
    }

    lcd->last_byte = byte;
    lcd->last_e    = e;
}

void i2c_lcd_get_lines(const i2c_lcd_t *lcd, char *line0, char *line1)
{
    for (int c = 0; c < LCD_COLS; c++) {
        uint8_t b0 = lcd->ddram[c];
        uint8_t b1 = lcd->ddram[LCD_COLS + c];
        /* Replace non-printable bytes with '?'. The HD44780 character
         * ROM spans 0x20..0x7F for ASCII plus an extended set; UI
         * fonts won't render the latter usefully. */
        line0[c] = (b0 >= 0x20 && b0 < 0x7F) ? (char)b0 : '?';
        line1[c] = (b1 >= 0x20 && b1 < 0x7F) ? (char)b1 : '?';
    }
    line0[LCD_COLS] = '\0';
    line1[LCD_COLS] = '\0';
}
