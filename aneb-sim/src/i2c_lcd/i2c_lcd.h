/*
 * i2c_lcd.h — pure-logic model of an HD44780-class 16x2 character LCD
 * driven through a PCF8574 I2C backpack (the "1602 I2C" module that's
 * standard for Arduino projects).
 *
 * No simavr dependency. The model is fed one PCF8574 byte at a time
 * via i2c_lcd_write_byte; tracks the E falling edge to assemble HD44780
 * nibbles into command/data bytes; maintains a 16x2 DDRAM buffer and
 * cursor; calls a user-supplied callback whenever the displayed text
 * changes.
 *
 * The simavr glue in sim_loop.c bridges this model to one AVR core's
 * TWI peripheral: matches the slave address, ACKs, and forwards each
 * data byte into i2c_lcd_write_byte.
 *
 * Tests link this module directly without simavr.
 */
#ifndef ANEB_I2C_LCD_H
#define ANEB_I2C_LCD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LCD_COLS 16
#define LCD_ROWS  2

/* Standard PCF8574 -> HD44780 wiring used by every "1602 I2C" board on
 * the market and by the LiquidCrystal_I2C library.
 *
 *   P0 -> RS   (register select: 0=command, 1=data)
 *   P1 -> RW   (read/write: tied LOW for write-only operation)
 *   P2 -> E    (enable: HD44780 latches on its falling edge)
 *   P3 -> BL   (backlight: active-high)
 *   P4 -> D4
 *   P5 -> D5
 *   P6 -> D6
 *   P7 -> D7   (D0..D3 unused; HD44780 runs in 4-bit mode)
 */
#define I2C_LCD_BIT_RS   (1u << 0)
#define I2C_LCD_BIT_RW   (1u << 1)
#define I2C_LCD_BIT_E    (1u << 2)
#define I2C_LCD_BIT_BL   (1u << 3)
#define I2C_LCD_MASK_D   0xF0u

/* Called whenever the visible display content changes. Arguments are
 * NUL-terminated strings of exactly LCD_COLS characters each. Pointers
 * are valid only for the duration of the callback. */
typedef void (*i2c_lcd_changed_cb)(void *ctx,
                                    const char *line0,
                                    const char *line1);

typedef struct i2c_lcd {
    /* Public — set before i2c_lcd_init. Callback fires on every visible
     * change (DDRAM write, clear, cursor wrap). */
    i2c_lcd_changed_cb on_changed;
    void              *ctx;
    char               id[16];      /* diagnostic, e.g. "ecu1.lcd" */
    uint8_t            i2c_addr;    /* 7-bit slave address (0x27 default) */

    /* Internal state — public so tests can inspect. Do not poke. */

    /* PCF8574 byte assembly */
    uint8_t  last_byte;
    bool     last_e;

    /* HD44780 nibble assembly */
    uint8_t  hi_nibble;
    bool     have_hi_nibble;
    bool     in_init_8bit;       /* true until 4-bit mode enabled */

    /* HD44780 visible state */
    uint8_t  ddram[LCD_ROWS * LCD_COLS];
    uint8_t  cursor_addr;        /* HD44780 DDRAM address: 0x00..0x0F or 0x40..0x4F */
    bool     auto_increment;     /* entry-mode I/D bit */
    bool     display_on;
    bool     backlight_on;
} i2c_lcd_t;

/* ----- Lifecycle ---------------------------------------------------- */

void i2c_lcd_init (i2c_lcd_t *lcd, const char *id, uint8_t i2c_addr);
void i2c_lcd_reset(i2c_lcd_t *lcd);

/* Feed one PCF8574 data byte into the model. The caller is responsible
 * for matching the slave address before calling — addresses don't get
 * passed in here, this is the wire data only. */
void i2c_lcd_write_byte(i2c_lcd_t *lcd, uint8_t byte);

/* Copy the current 16-character lines into caller-provided buffers.
 * Each buffer must hold at least LCD_COLS+1 bytes. Empty cells are
 * filled with ASCII space. */
void i2c_lcd_get_lines(const i2c_lcd_t *lcd, char *line0, char *line1);

#endif /* ANEB_I2C_LCD_H */
