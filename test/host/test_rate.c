#include "../../src/util.h"
#include "test.h"

#define HZ 147456000ull /* EE bus clock: GetTimerSystemTime() ticks per second */

TEST(rate_mib10_from_timer_ticks) {
  CHECK_EQ_INT(rate_mib10(10ull << 20, HZ, HZ), 100); /* 10 MiB in 1 s */
  CHECK_EQ_INT(rate_mib10(7ull << 19, HZ, HZ), 35);   /* 3.5 MiB/s */
  CHECK_EQ_INT(rate_mib10(4ull << 30, 400 * HZ, HZ), 102); /* 4 GiB, 400 s */
  CHECK_EQ_INT(rate_mib10(1ull << 20, 0, HZ), 0); /* nothing timed yet */
  CHECK_EQ_INT(rate_mib10(0, HZ, HZ), 0);
  /* 8 GiB in 0.1 s must not overflow the intermediate product */
  CHECK_EQ_INT(rate_mib10(8ull << 30, HZ / 10, HZ), 819200);
}
