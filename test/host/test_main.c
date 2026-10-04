#include <stdlib.h>

#include "test.h"

#define MAX_TESTS 512

static struct {
  const char *name;
  test_fn fn;
} tests[MAX_TESTS];
static int test_count;
int test_failures;
const char *test_current = "";

void test_register(const char *name, test_fn fn) {
  if (test_count < MAX_TESTS) {
    tests[test_count].name = name;
    tests[test_count].fn = fn;
    test_count++;
  }
}

int main(int argc, char **argv) {
  int failed_tests = 0;
  for (int i = 0; i < test_count; i++) {
    if (argc > 1 && !strstr(tests[i].name, argv[1]))
      continue;
    int before = test_failures;
    test_current = tests[i].name;
    tests[i].fn();
    if (test_failures != before) {
      failed_tests++;
      printf("FAIL %s\n", tests[i].name);
    } else {
      printf("ok   %s\n", tests[i].name);
    }
  }
  printf("\n%d tests, %d failed, %d failed checks\n", test_count, failed_tests,
         test_failures);
  return failed_tests ? 1 : 0;
}
