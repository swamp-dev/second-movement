// Minimal host-side test helpers. Each test file is its own program: a failed
// CHECK is reported and the test carries on, and check_summary() returns the
// exit code.

#ifndef CHECK_H_
#define CHECK_H_

#include <stdio.h>

static int check_failures;
static int check_count;

#define CHECK(cond) do { \
    check_count++; \
    if (!(cond)) { \
        check_failures++; \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

// CHECK_EQ_INT(actual, expected, printf-style context...)
#define CHECK_EQ_INT(actual, expected, ...) do { \
    long check_a_ = (long)(actual), check_e_ = (long)(expected); \
    check_count++; \
    if (check_a_ != check_e_) { \
        check_failures++; \
        fprintf(stderr, "%s:%d: %s == %ld, expected %ld: ", __FILE__, __LINE__, #actual, check_a_, check_e_); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } \
} while (0)

static inline int check_summary(const char *name) {
    printf("%s: %d checks, %d failed\n", name, check_count, check_failures);
    return check_failures ? 1 : 0;
}

#endif // CHECK_H_
