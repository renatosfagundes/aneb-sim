/*
 * test.h — minimal test harness for aneb-sim unit tests.
 *
 * Pattern:
 *
 *   static int test_my_thing(void) {
 *       TEST_ASSERT_EQ(1 + 1, 2, "math is broken");
 *       return 0;
 *   }
 *
 *   int main(void) {
 *       TEST_BEGIN();
 *       TEST_RUN(test_my_thing);
 *       return TEST_END();
 *   }
 */
#ifndef ANEB_TEST_H
#define ANEB_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_test_pass;
extern int g_test_fail;
extern const char *g_test_current;

#define TEST_BEGIN() do { g_test_pass = 0; g_test_fail = 0; } while (0)

#define TEST_END() ( \
    fprintf(stderr, "\n%d passed, %d failed\n", g_test_pass, g_test_fail), \
    (g_test_fail == 0 ? 0 : 1) \
)

#define TEST_RUN(fn) do {                                  \
    g_test_current = #fn;                                  \
    int _rc = fn();                                        \
    if (_rc == 0) { g_test_pass++; fputc('.', stderr); }   \
    else          { g_test_fail++; fputc('F', stderr); }   \
    fflush(stderr);                                        \
} while (0)

#define TEST_ASSERT(cond, ...) do {                        \
    if (!(cond)) {                                         \
        fprintf(stderr,                                    \
                "\n  FAIL %s @ %s:%d: ",                   \
                g_test_current, __FILE__, __LINE__);       \
        fprintf(stderr, __VA_ARGS__);                      \
        fputc('\n', stderr);                               \
        return -1;                                         \
    }                                                      \
} while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    TEST_ASSERT((long long)(a) == (long long)(b),          \
                "%s — got %lld, expected %lld",            \
                (msg), (long long)(a), (long long)(b))

#define TEST_ASSERT_EQ_HEX(a, b, msg) \
    TEST_ASSERT((long long)(a) == (long long)(b),          \
                "%s — got 0x%llx, expected 0x%llx",        \
                (msg), (long long)(a), (long long)(b))

#endif /* ANEB_TEST_H */
