#include <string.h>

#include "../../src/hdd_health.h"
#include "../../src/hdl_plan.h"
#include "../../src/source_cdvd.h"
#include "test.h"

static void put_attr(uint8_t *d, int slot, uint8_t id, uint8_t value, uint64_t raw) {
  uint8_t *e = d + 2 + 12 * slot;
  e[0] = id;
  e[3] = value;
  e[4] = value;
  for (int b = 0; b < 6; b++)
    e[5 + b] = (uint8_t)(raw >> (8 * b));
}

static void seal(uint8_t *d) {
  uint8_t sum = 0;
  for (int i = 0; i < 511; i++)
    sum = (uint8_t)(sum + d[i]);
  d[511] = (uint8_t)(0x100 - sum);
}

TEST(smart_parses_attributes_and_checksum) {
  uint8_t d[512];
  memset(d, 0, sizeof(d));
  d[0] = 0x10;
  put_attr(d, 0, 5, 100, 0);
  put_attr(d, 2, 194, 60, 0x0012001E002Full); /* temperature in the low byte */
  put_attr(d, 3, 9, 90, 12345);
  seal(d);
  smart_attr_t a[SMART_MAX_ATTR];
  int n = smart_parse(d, a, SMART_MAX_ATTR);
  CHECK_EQ_INT(n, 3); /* empty slot 1 skipped */
  CHECK_EQ_INT(a[1].id, 194);
  CHECK_EQ_U64(smart_attr_display(&a[1]), 0x2F);
  CHECK_EQ_U64(smart_attr_display(&a[2]), 12345);
  CHECK_STR(smart_attr_name(197), "Pending sectors");
  CHECK(smart_attr_name(250) == NULL);
  d[100] ^= 1; /* bad checksum */
  CHECK_EQ_INT(smart_parse(d, a, SMART_MAX_ATTR), -1);
  memset(d, 0, sizeof(d)); /* all zero: nothing came back */
  CHECK_EQ_INT(smart_parse(d, a, SMART_MAX_ATTR), -1);
}

TEST(smart_verdicts) {
  smart_attr_t a[2] = {{5, 100, 100, 0}, {197, 100, 100, 0}};
  CHECK_EQ_INT(smart_verdict(0, a, 2), HEALTH_GOOD);
  a[1].raw = 3;
  CHECK_EQ_INT(smart_verdict(0, a, 2), HEALTH_WATCH);
  CHECK_EQ_INT(smart_verdict(1, a, 2), HEALTH_FAILING);
  CHECK_EQ_INT(smart_verdict(-5, NULL, 0), HEALTH_UNKNOWN);
  a[1].raw = 0;
  CHECK_EQ_INT(smart_verdict(-5, a, 2), HEALTH_GOOD); /* data without status */
}

TEST(largest_game_is_the_planner_limit) {
  uint64_t g = hdd_largest_game(10000, 4096);
  CHECK(g > 0 && g % (1024 * 1024) == 0);
  hdl_alloc_t a;
  CHECK_EQ_INT(hdl_plan_alloc(g, 4096, &a), ERR_OK);
  CHECK(a.total_mb <= 10000);
  int over = hdl_plan_alloc(g + 1024 * 1024, 4096, &a) != ERR_OK || a.total_mb > 10000;
  CHECK(over);
  CHECK_EQ_U64(hdd_largest_game(0, 4096), 0);
  CHECK_EQ_U64(hdd_largest_game(64, 4096), 0); /* below the smallest partition */
}

TEST(cdvd_disc_classes) {
  CHECK_EQ_INT(cdvd_classify(0x14), CDVD_DISC_PS2_DVD);
  CHECK_EQ_INT(cdvd_classify(0x12), CDVD_DISC_PS2_CD);
  CHECK_EQ_INT(cdvd_classify(0x13), CDVD_DISC_PS2_CD);
  CHECK_EQ_INT(cdvd_classify(0x10), CDVD_DISC_PS1);
  CHECK_EQ_INT(cdvd_classify(0x00), CDVD_DISC_NONE);
  CHECK_EQ_INT(cdvd_classify(0x01), CDVD_DISC_NONE);
  CHECK_EQ_INT(cdvd_classify(0xFE), CDVD_DISC_OTHER); /* DVD video */
}
