#ifndef PSXI_TEST_H
#define PSXI_TEST_H

#include <stdio.h>
#include <string.h>

/* Minimal host test harness. Each TEST() registers itself through a
 * constructor so test files only need to define tests. */

typedef void (*test_fn)(void);
void test_register(const char *name, test_fn fn);
extern int test_failures;
extern const char *test_current;

#define TEST(name)                                                             \
  static void name(void);                                                      \
  __attribute__((constructor)) static void reg_##name(void) {                  \
    test_register(#name, name);                                                \
  }                                                                            \
  static void name(void)

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "  FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__,          \
              test_current, #cond);                                            \
      test_failures++;                                                         \
    }                                                                          \
  } while (0)

#define CHECK_EQ_INT(a, b)                                                     \
  do {                                                                         \
    long long _a = (long long)(a), _b = (long long)(b);                        \
    if (_a != _b) {                                                            \
      fprintf(stderr, "  FAIL %s:%d [%s]: %s == %s (%lld != %lld)\n",         \
              __FILE__, __LINE__, test_current, #a, #b, _a, _b);              \
      test_failures++;                                                         \
    }                                                                          \
  } while (0)

#define CHECK_EQ_U64(a, b)                                                     \
  do {                                                                         \
    unsigned long long _a = (unsigned long long)(a),                           \
                       _b = (unsigned long long)(b);                           \
    if (_a != _b) {                                                            \
      fprintf(stderr, "  FAIL %s:%d [%s]: %s == %s (%llu != %llu)\n",         \
              __FILE__, __LINE__, test_current, #a, #b, _a, _b);              \
      test_failures++;                                                         \
    }                                                                          \
  } while (0)

#define CHECK_STR(a, b)                                                        \
  do {                                                                         \
    const char *_a = (a), *_b = (b);                                           \
    if (strcmp(_a, _b) != 0) {                                                 \
      fprintf(stderr, "  FAIL %s:%d [%s]: %s == \"%s\" (got \"%s\")\n",       \
              __FILE__, __LINE__, test_current, #a, _b, _a);                  \
      test_failures++;                                                         \
    }                                                                          \
  } while (0)

#endif
