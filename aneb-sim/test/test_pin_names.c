/*
 * test_pin_names.c — pin name parsing, Arduino aliases, ADC channel
 * resolution.
 */
#include "test.h"
#include "pin_names.h"

int test_port_form(void)
{
    char p; int b;
    TEST_ASSERT_EQ(pin_parse("PB5", &p, &b), 0, "PB5 parses");
    TEST_ASSERT_EQ(p, 'B', "port B");
    TEST_ASSERT_EQ(b, 5,   "bit 5");

    TEST_ASSERT_EQ(pin_parse("PD2", &p, &b), 0, "PD2 parses");
    TEST_ASSERT_EQ(p, 'D', "port D"); TEST_ASSERT_EQ(b, 2, "bit 2");

    /* Lowercase/mixed case is accepted (case-insensitive on letters). */
    TEST_ASSERT_EQ(pin_parse("pb5", &p, &b), 0, "lowercase pb5 parses");

    /* Bad inputs. */
    TEST_ASSERT(pin_parse("PA0", &p, &b) < 0, "PA not on 328P");
    TEST_ASSERT(pin_parse("PB8", &p, &b) < 0, "bit out of range");
    TEST_ASSERT(pin_parse("PB",  &p, &b) < 0, "missing bit");
    TEST_ASSERT(pin_parse("PB55",&p, &b) < 0, "extra digit");
    return 0;
}

int test_arduino_digital(void)
{
    char p; int b;

    /* D0..D7 -> PD0..PD7 */
    TEST_ASSERT_EQ(pin_parse("D0", &p, &b), 0, "D0"); TEST_ASSERT_EQ(p, 'D', ""); TEST_ASSERT_EQ(b, 0, "");
    TEST_ASSERT_EQ(pin_parse("D7", &p, &b), 0, "D7"); TEST_ASSERT_EQ(p, 'D', ""); TEST_ASSERT_EQ(b, 7, "");

    /* D8..D13 -> PB0..PB5 */
    TEST_ASSERT_EQ(pin_parse("D8", &p, &b), 0, "D8");  TEST_ASSERT_EQ(p, 'B', ""); TEST_ASSERT_EQ(b, 0, "");
    TEST_ASSERT_EQ(pin_parse("D13",&p, &b), 0, "D13"); TEST_ASSERT_EQ(p, 'B', ""); TEST_ASSERT_EQ(b, 5, "");

    TEST_ASSERT(pin_parse("D14", &p, &b) < 0, "D14 out of range");
    TEST_ASSERT(pin_parse("D-1", &p, &b) < 0, "negative rejected");
    return 0;
}

int test_arduino_analog_as_digital(void)
{
    char p; int b;

    /* A0..A5 -> PC0..PC5 */
    TEST_ASSERT_EQ(pin_parse("A0", &p, &b), 0, "A0"); TEST_ASSERT_EQ(p, 'C', ""); TEST_ASSERT_EQ(b, 0, "");
    TEST_ASSERT_EQ(pin_parse("A4", &p, &b), 0, "A4"); TEST_ASSERT_EQ(p, 'C', ""); TEST_ASSERT_EQ(b, 4, "");
    TEST_ASSERT_EQ(pin_parse("A5", &p, &b), 0, "A5"); TEST_ASSERT_EQ(p, 'C', ""); TEST_ASSERT_EQ(b, 5, "");

    /* A6, A7 are ADC-only. */
    TEST_ASSERT(pin_parse("A6", &p, &b) < 0, "A6 has no digital path");
    TEST_ASSERT(pin_parse("A7", &p, &b) < 0, "A7 has no digital path");
    return 0;
}

int test_pin_format_round_trip(void)
{
    char p; int b;
    pin_parse("D5", &p, &b);
    const char *s = pin_format(p, b);
    TEST_ASSERT(s[0] == 'P' && s[1] == 'D' && s[2] == '5' && s[3] == 0,
                "D5 formats as PD5");
    return 0;
}

int test_adc_channel_parse_arduino(void)
{
    TEST_ASSERT_EQ(adc_channel_parse("A0"), 0, "A0 -> 0");
    TEST_ASSERT_EQ(adc_channel_parse("A7"), 7, "A7 -> 7");
    TEST_ASSERT_EQ(adc_channel_parse("a3"), 3, "lowercase a3");
    TEST_ASSERT_EQ(adc_channel_parse("3"),  3, "bare 3");
    TEST_ASSERT(adc_channel_parse("A8")  < 0, "A8 out of range");
    TEST_ASSERT(adc_channel_parse("X1")  < 0, "X1 invalid");
    TEST_ASSERT(adc_channel_parse("")    < 0, "empty");
    return 0;
}
