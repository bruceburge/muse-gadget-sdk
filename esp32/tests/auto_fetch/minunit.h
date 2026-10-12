#pragma once
// Minimal C test framework (no external dependencies).
// Usage:
//   MU_TEST(my_test) { mu_assert(x == 1); }
//   int main(void) { MU_RUN(my_test); MU_SUMMARY(); }
#include <stdio.h>
#include <string.h>

static int mu_tests_run = 0;
static int mu_tests_failed = 0;
static int mu_current_failed = 0;
static const char *mu_current_name = "";

#define MU_TEST(name) static void name(void)

#define mu_assert(cond) \
    do { \
        if (!(cond)) { \
            printf("    FAIL %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
            mu_current_failed = 1; \
            return; \
        } \
    } while (0)

#define mu_assert_int_eq(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        if (_a != _b) { \
            printf("    FAIL %s:%d: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, _a, #b, _b); \
            mu_current_failed = 1; \
            return; \
        } \
    } while (0)

#define mu_assert_str_eq(a, b) \
    do { \
        const char *_a = (a), *_b = (b); \
        if (strcmp(_a, _b) != 0) { \
            printf("    FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); \
            mu_current_failed = 1; \
            return; \
        } \
    } while (0)

#define MU_RUN(name) \
    do { \
        mu_current_name = #name; \
        mu_current_failed = 0; \
        mu_tests_run++; \
        name(); \
        if (mu_current_failed) { \
            mu_tests_failed++; \
            printf("  [FAIL] %s\n", #name); \
        } else { \
            printf("  [ ok ] %s\n", #name); \
        } \
    } while (0)

#define MU_SUMMARY() \
    do { \
        printf("\n%d tests run, %d failed\n", mu_tests_run, mu_tests_failed); \
        return mu_tests_failed ? 1 : 0; \
    } while (0)
