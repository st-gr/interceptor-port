/* tests/test.h - tiny test harness, no dependencies. */
#ifndef INTERCEPTOR_TEST_H
#define INTERCEPTOR_TEST_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_failures = 0;
static int test_count = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { int _before = test_failures; test_count++; printf("  %-56s", #name); name(); \
    if (test_failures == _before) printf("ok\n"); } while (0)
#define FAIL(msg) do { test_failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); return; } while (0)
#define ASSERT_TRUE(c) do { if (!(c)) FAIL(#c); } while (0)
#define ASSERT_EQ_INT(a, b) do { long _a = (long)(a), _b = (long)(b); \
    if (_a != _b) { printf("  expected %ld got %ld\n", _b, _a); FAIL(#a " == " #b); } } while (0)
#define ASSERT_STR_EQ(a, b) do { if (strcmp((a), (b)) != 0) { printf("  got \"%s\"\n", (a)); FAIL(#a " == " #b); } } while (0)
#define TEST_MAIN_END() do { printf("%d tests, %d failures\n", test_count, test_failures); return test_failures ? 1 : 0; } while (0)

#endif
