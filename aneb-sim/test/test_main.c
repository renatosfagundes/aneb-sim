/*
 * test_main.c — runs every M2 test suite in a single binary.
 *
 * Per-suite registration is just a static prototype + TEST_RUN call here.
 * Adding a suite means adding a TEST_RUN line and an extern declaration.
 */
#include "test.h"

int g_test_pass = 0;
int g_test_fail = 0;
const char *g_test_current = "(none)";

/* mcp2515 register suite */
extern int test_reset_values(void);
extern int test_write_then_read(void);
extern int test_write_auto_increment(void);
extern int test_read_auto_increment(void);
extern int test_bit_modify(void);
extern int test_canstat_readonly(void);
extern int test_mode_transition_via_canctrl(void);
extern int test_reset_via_spi_command(void);

/* mcp2515 SPI command suite */
extern int test_read_status_idle(void);
extern int test_rx_status_idle(void);
extern int test_rts_sets_txreq(void);
extern int test_load_tx_full(void);
extern int test_load_tx_data_only(void);
extern int test_read_rx_clears_flag(void);

/* mcp2515 filter suite */
extern int test_filter_exact_match_std(void);
extern int test_filter_mask_zero_accepts_all(void);
extern int test_filter_extended_id(void);
extern int test_filter_std_vs_ext_disagreement(void);
extern int test_rxm_recv_any_bypasses_filters(void);
extern int test_rxm_recv_std_rejects_ext(void);
extern int test_rxb0_filhit_recorded(void);

/* mcp2515 loopback suite */
extern int test_loopback_round_trip(void);
extern int test_loopback_int_pin_asserted(void);
extern int test_loopback_txif_set(void);
extern int test_loopback_filtered_out(void);

/* can_bus suite */
extern int test_bus_attach(void);
extern int test_bus_broadcast_skips_source(void);
extern int test_bus_broadcast_to_multiple_peers(void);
extern int test_bus_inject_no_source_skip(void);
extern int test_bus_filter_end_to_end(void);
extern int test_bus_mode_gating(void);
extern int test_bus_normal_tx_through_callback(void);

/* pin_names suite (M5) */
extern int test_port_form(void);
extern int test_arduino_digital(void);
extern int test_arduino_analog_as_digital(void);
extern int test_pin_format_round_trip(void);
extern int test_adc_channel_parse_arduino(void);

/* i2c_lcd suite */
extern int test_lcd_init_clears_buffer(void);
extern int test_lcd_print_after_init(void);
extern int test_lcd_set_cursor_second_row(void);
extern int test_lcd_clear_resets_cursor(void);
extern int test_lcd_callback_fires_on_data_write(void);
extern int test_lcd_non_printable_becomes_question_mark(void);
extern int test_lcd_init_pulses_dont_corrupt_state(void);
extern int test_lcd_unselected_address_ignored(void);

/* mcp2515 errors suite */
extern int test_initial_state_active(void);
extern int test_inject_tx_error_increments_tec(void);
extern int test_inject_rx_error_increments_rec(void);
extern int test_warning_threshold_eflg(void);
extern int test_passive_threshold_via_tec(void);
extern int test_passive_threshold_via_rec(void);
extern int test_busoff_threshold(void);
extern int test_force_busoff_helper(void);
extern int test_force_error_passive_helper(void);
extern int test_busoff_disables_tx(void);
extern int test_busoff_disables_rx(void);
extern int test_recover_via_helper(void);
extern int test_recover_via_canctrl_mode_change(void);
extern int test_successful_tx_decrements_tec(void);
extern int test_successful_rx_decrements_rec(void);
extern int test_eflg_overflow_sticky_on_state_update(void);

int main(void)
{
    TEST_BEGIN();

    fputs("registers   ", stderr);
    TEST_RUN(test_reset_values);
    TEST_RUN(test_write_then_read);
    TEST_RUN(test_write_auto_increment);
    TEST_RUN(test_read_auto_increment);
    TEST_RUN(test_bit_modify);
    TEST_RUN(test_canstat_readonly);
    TEST_RUN(test_mode_transition_via_canctrl);
    TEST_RUN(test_reset_via_spi_command);
    fputc('\n', stderr);

    fputs("spi cmds    ", stderr);
    TEST_RUN(test_read_status_idle);
    TEST_RUN(test_rx_status_idle);
    TEST_RUN(test_rts_sets_txreq);
    TEST_RUN(test_load_tx_full);
    TEST_RUN(test_load_tx_data_only);
    TEST_RUN(test_read_rx_clears_flag);
    fputc('\n', stderr);

    fputs("filters     ", stderr);
    TEST_RUN(test_filter_exact_match_std);
    TEST_RUN(test_filter_mask_zero_accepts_all);
    TEST_RUN(test_filter_extended_id);
    TEST_RUN(test_filter_std_vs_ext_disagreement);
    TEST_RUN(test_rxm_recv_any_bypasses_filters);
    TEST_RUN(test_rxm_recv_std_rejects_ext);
    TEST_RUN(test_rxb0_filhit_recorded);
    fputc('\n', stderr);

    fputs("loopback    ", stderr);
    TEST_RUN(test_loopback_round_trip);
    TEST_RUN(test_loopback_int_pin_asserted);
    TEST_RUN(test_loopback_txif_set);
    TEST_RUN(test_loopback_filtered_out);
    fputc('\n', stderr);

    fputs("can_bus     ", stderr);
    TEST_RUN(test_bus_attach);
    TEST_RUN(test_bus_broadcast_skips_source);
    TEST_RUN(test_bus_broadcast_to_multiple_peers);
    TEST_RUN(test_bus_inject_no_source_skip);
    TEST_RUN(test_bus_filter_end_to_end);
    TEST_RUN(test_bus_mode_gating);
    TEST_RUN(test_bus_normal_tx_through_callback);
    fputc('\n', stderr);

    fputs("pin names   ", stderr);
    TEST_RUN(test_port_form);
    TEST_RUN(test_arduino_digital);
    TEST_RUN(test_arduino_analog_as_digital);
    TEST_RUN(test_pin_format_round_trip);
    TEST_RUN(test_adc_channel_parse_arduino);
    fputc('\n', stderr);

    fputs("i2c lcd     ", stderr);
    TEST_RUN(test_lcd_init_clears_buffer);
    TEST_RUN(test_lcd_print_after_init);
    TEST_RUN(test_lcd_set_cursor_second_row);
    TEST_RUN(test_lcd_clear_resets_cursor);
    TEST_RUN(test_lcd_callback_fires_on_data_write);
    TEST_RUN(test_lcd_non_printable_becomes_question_mark);
    TEST_RUN(test_lcd_init_pulses_dont_corrupt_state);
    TEST_RUN(test_lcd_unselected_address_ignored);
    fputc('\n', stderr);

    fputs("errors      ", stderr);
    TEST_RUN(test_initial_state_active);
    TEST_RUN(test_inject_tx_error_increments_tec);
    TEST_RUN(test_inject_rx_error_increments_rec);
    TEST_RUN(test_warning_threshold_eflg);
    TEST_RUN(test_passive_threshold_via_tec);
    TEST_RUN(test_passive_threshold_via_rec);
    TEST_RUN(test_busoff_threshold);
    TEST_RUN(test_force_busoff_helper);
    TEST_RUN(test_force_error_passive_helper);
    TEST_RUN(test_busoff_disables_tx);
    TEST_RUN(test_busoff_disables_rx);
    TEST_RUN(test_recover_via_helper);
    TEST_RUN(test_recover_via_canctrl_mode_change);
    TEST_RUN(test_successful_tx_decrements_tec);
    TEST_RUN(test_successful_rx_decrements_rec);
    TEST_RUN(test_eflg_overflow_sticky_on_state_update);
    fputc('\n', stderr);

    return TEST_END();
}
