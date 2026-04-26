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

    return TEST_END();
}
