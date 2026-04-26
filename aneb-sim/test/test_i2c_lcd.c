/*
 * test_i2c_lcd.c — exercises the PCF8574 nibble-assembly + HD44780
 * decoder, end-to-end at the byte stream level (i.e. exactly what the
 * simavr glue feeds into the model).
 *
 * Each test drives the LCD with a sequence of bytes that the standard
 * LiquidCrystal_I2C library would emit, then asserts on the visible
 * 16x2 buffer.
 */
#include "test.h"

#include "i2c_lcd.h"

#include <string.h>

/* ----- helpers ----------------------------------------------------- */

/* Pulse one HD44780 nibble through the PCF8574: write byte with E
 * high, then write the same byte with E low. RS is encoded into the
 * caller's provided byte. Backlight is left on for clarity. */
static void pulse(i2c_lcd_t *lcd, uint8_t base)
{
    i2c_lcd_write_byte(lcd, base | I2C_LCD_BIT_E);
    i2c_lcd_write_byte(lcd, base & ~I2C_LCD_BIT_E);
}

/* Send one full HD44780 byte (command or data) as two consecutive
 * nibbles, exactly the way LiquidCrystal_I2C::send() does it. */
static void send_byte(i2c_lcd_t *lcd, uint8_t value, bool is_data)
{
    uint8_t rs   = is_data ? I2C_LCD_BIT_RS : 0;
    uint8_t bl   = I2C_LCD_BIT_BL;
    uint8_t hi   = (uint8_t)(value & 0xF0);
    uint8_t lo   = (uint8_t)((value << 4) & 0xF0);
    pulse(lcd, hi | rs | bl);
    pulse(lcd, lo | rs | bl);
}

/* Replay the standard 4-bit init sequence so we leave the in_init_8bit
 * phase before the actual test commands run. */
static void run_init(i2c_lcd_t *lcd)
{
    uint8_t bl = I2C_LCD_BIT_BL;
    pulse(lcd, 0x30 | bl);   /* function set, 8-bit (x3) */
    pulse(lcd, 0x30 | bl);
    pulse(lcd, 0x30 | bl);
    pulse(lcd, 0x20 | bl);   /* function set, 4-bit — leaves init phase */
}

static void cmd (i2c_lcd_t *lcd, uint8_t c) { send_byte(lcd, c, false); }
static void data(i2c_lcd_t *lcd, uint8_t b) { send_byte(lcd, b, true);  }

static void print_str(i2c_lcd_t *lcd, const char *s)
{
    while (*s) data(lcd, (uint8_t)*s++);
}

static void set_cursor(i2c_lcd_t *lcd, int row, int col)
{
    uint8_t addr = (row ? 0x40 : 0x00) + (uint8_t)col;
    cmd(lcd, (uint8_t)(0x80 | addr));
}

/* Ignore the changed callback for most tests; we read state directly. */
static void noop_changed(void *_ctx, const char *_l0, const char *_l1)
{
    (void)_ctx; (void)_l0; (void)_l1;
}

/* Callback under test in test_lcd_callback_fires_on_data_write — file
 * scope because C doesn't allow nested functions. */
static int  g_cb_calls = 0;
static char g_cb_l0[17];
static char g_cb_l1[17];
static void capture_changed(void *ctx, const char *l0, const char *l1)
{
    (void)ctx;
    g_cb_calls++;
    memcpy(g_cb_l0, l0, 17);
    memcpy(g_cb_l1, l1, 17);
}

/* ----- tests ------------------------------------------------------- */

int test_lcd_init_clears_buffer(void)
{
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(strcmp(l0, "                ") == 0, "row0 not blank");
    TEST_ASSERT(strcmp(l1, "                ") == 0, "row1 not blank");
    return 0;
}

int test_lcd_print_after_init(void)
{
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);

    cmd(&lcd, 0x01);            /* clear */
    set_cursor(&lcd, 0, 0);
    print_str(&lcd, "Hello");

    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(strncmp(l0, "Hello", 5) == 0, "row0 missing 'Hello'");
    return 0;
}

int test_lcd_set_cursor_second_row(void)
{
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);

    set_cursor(&lcd, 1, 3);
    print_str(&lcd, "OK");

    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(l1[3] == 'O' && l1[4] == 'K',
                "row1 missing 'OK' at col 3");
    TEST_ASSERT(strcmp(l0, "                ") == 0,
                "row0 unexpectedly written");
    return 0;
}

int test_lcd_clear_resets_cursor(void)
{
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);

    set_cursor(&lcd, 1, 5);
    print_str(&lcd, "junk");
    cmd(&lcd, 0x01);            /* clear */
    print_str(&lcd, "X");       /* should land at row 0 col 0 */

    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(l0[0] == 'X', "post-clear write didn't land at home");
    TEST_ASSERT(l1[5] == ' ', "row1 still showing pre-clear text");
    return 0;
}

int test_lcd_callback_fires_on_data_write(void)
{
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);

    g_cb_calls = 0;
    memset(g_cb_l0, 0, sizeof(g_cb_l0));
    memset(g_cb_l1, 0, sizeof(g_cb_l1));
    lcd.on_changed = capture_changed;
    lcd.ctx        = NULL;

    set_cursor(&lcd, 0, 0);
    print_str(&lcd, "Hi");

    TEST_ASSERT(g_cb_calls >= 2,
                "callback should fire at least once per data write");
    TEST_ASSERT(g_cb_l0[0] == 'H' && g_cb_l0[1] == 'i',
                "callback received stale buffer");
    return 0;
}

int test_lcd_non_printable_becomes_question_mark(void)
{
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);
    lcd.on_changed = noop_changed;

    set_cursor(&lcd, 0, 0);
    data(&lcd, 0x05);       /* CGRAM custom char index — not modeled */
    data(&lcd, 'B');

    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(l0[0] == '?', "non-printable byte not folded to '?'");
    TEST_ASSERT(l0[1] == 'B', "next char clobbered after '?'");
    return 0;
}

int test_lcd_init_pulses_dont_corrupt_state(void)
{
    /* The init sequence must NOT be interpreted as 4-bit nibbles —
     * otherwise the three 0x3 pulses would pair up into ad-hoc bytes
     * (e.g. 0x33) and DDRAM would already be dirty before the user's
     * first command runs. */
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);

    /* Without any user writes, both rows must still be blank. */
    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(strcmp(l0, "                ") == 0,
                "init sequence dirtied row0");
    TEST_ASSERT(strcmp(l1, "                ") == 0,
                "init sequence dirtied row1");
    /* And we should be in 4-bit mode — which means a real command
     * (clear) only takes one full byte, not a single nibble. */
    cmd(&lcd, 0x01);
    return 0;
}

int test_lcd_unselected_address_ignored(void)
{
    /* The pure-logic model itself doesn't track selection — that's the
     * job of the simavr glue. But we should still verify the byte
     * write API tolerates being called with random data without ever
     * crashing or producing weird state. */
    i2c_lcd_t lcd;
    i2c_lcd_init(&lcd, "test", 0x27);
    run_init(&lcd);

    /* Random PCF8574 byte without ever raising E doesn't update DDRAM.
     * Mask out bit 2 (E) to ensure no falling edge ever occurs. */
    for (int i = 0; i < 50; i++) {
        uint8_t b = (uint8_t)(0xA0 | (i & 0x0F));
        b &= (uint8_t)~I2C_LCD_BIT_E;
        i2c_lcd_write_byte(&lcd, b);
    }
    char l0[17], l1[17];
    i2c_lcd_get_lines(&lcd, l0, l1);
    TEST_ASSERT(strcmp(l0, "                ") == 0,
                "no-E writes shouldn't move state");
    return 0;
}
