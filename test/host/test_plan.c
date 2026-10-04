#include "../../src/hdl_plan.h"
#include "test.h"

#define MB (1024ull * 1024ull)
#define S2K 2048ull

static const uint32_t MAXES[] = {128, 256, 512, 1024, 2048, 4096};

/* Checks every plan must satisfy: buckets within the drive maximum,
 * capacity covers the data, hdlfs's sequential fill leaves no empty
 * partition, no slice reaches HDLFS's 4 GiB overflow, and the last
 * partition could not have been a smaller bucket. */
static void check_plan(uint64_t bytes, uint32_t max_mb, const hdl_alloc_t *a) {
  CHECK(a->main_mb <= max_mb);
  for (int i = 0; i < a->subs; i++)
    CHECK(a->sub_mb[i] <= max_mb);
  CHECK(a->subs <= HDL_MAX_SUBS);
  CHECK(hdl_alloc_capacity(a) >= bytes);

  uint32_t fill[HDL_MAX_SUBS + 1];
  int n = hdl_plan_fill(a, bytes, fill);
  CHECK_EQ_INT(n, 1 + a->subs);
  uint64_t sum = 0;
  for (int i = 0; i < n; i++) {
    CHECK(fill[i] > 0);          /* no empty partition */
    CHECK(fill[i] < 0x200000u);  /* hdlfs slice-size limit */
    sum += fill[i];
  }
  CHECK_EQ_U64(sum, bytes / S2K);

  /* Minimality of the last bucket: one size smaller would not fit. */
  uint32_t last = a->subs ? a->sub_mb[a->subs - 1] : a->main_mb;
  if (last > 128) {
    uint64_t smaller = (uint64_t)(last / 2) * 512 - (a->subs ? HDL_SUB_RESERVE_2K : HDL_MAIN_RESERVE_2K);
    CHECK(fill[n - 1] > smaller);
  }
}

TEST(plan_capacity_and_fill_all_maxes) {
  for (unsigned m = 0; m < sizeof(MAXES) / sizeof(MAXES[0]); m++) {
    for (uint64_t mb = 1; mb < 4700; mb += 61) {
      hdl_alloc_t a;
      uint64_t bytes = mb * MB - 2 * S2K;
      inst_err_t e = hdl_plan_alloc(bytes, MAXES[m], &a);
      if (e == ERR_OK)
        check_plan(bytes, MAXES[m], &a);
      else
        CHECK_EQ_INT(e, ERR_HDL_PLAN); /* only when > 64 subs are needed */
    }
  }
}

TEST(plan_max_128) {
  hdl_alloc_t a;
  /* 600 MiB: 124 MiB in main, then 128 MiB subs (128 MiB - 2 KiB each). */
  CHECK_EQ_INT(hdl_plan_alloc(600 * MB, 128, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "128M");
  CHECK_EQ_INT(a.subs, 4);
  for (int i = 0; i < a.subs; i++)
    CHECK_STR(a.sub_size_str[i], "128M");
  check_plan(600 * MB, 128, &a);
  /* 7900 MiB still fits (61 subs); a full DVD9 needs 67 > APA_MAXSUB. */
  CHECK_EQ_INT(hdl_plan_alloc(7900 * MB, 128, &a), ERR_OK);
  CHECK_EQ_INT(a.subs, 61);
  check_plan(7900 * MB, 128, &a);
  CHECK_EQ_INT(hdl_plan_alloc(8704 * MB, 128, &a), ERR_HDL_PLAN);
}

TEST(plan_max_256_512_1g_2g) {
  hdl_alloc_t a;
  CHECK_EQ_INT(hdl_plan_alloc(1000 * MB, 256, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "256M");
  check_plan(1000 * MB, 256, &a);
  CHECK_EQ_INT(hdl_plan_alloc(1000 * MB, 512, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "512M");
  check_plan(1000 * MB, 512, &a);
  CHECK_EQ_INT(hdl_plan_alloc(1000 * MB, 1024, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "1G");
  CHECK_EQ_INT(a.subs, 0);
  CHECK_EQ_INT(hdl_plan_alloc(4000 * MB, 2048, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "2G");
  CHECK_EQ_INT(a.subs, 1);
  CHECK_STR(a.sub_size_str[0], "2G");
  check_plan(4000 * MB, 2048, &a);
}

TEST(plan_max_4g) {
  hdl_alloc_t a;
  CHECK_EQ_INT(hdl_plan_alloc(S2K, 4096, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "128M");
  CHECK_EQ_INT(hdl_plan_alloc(8704 * MB, 4096, &a), ERR_OK);
  CHECK_EQ_INT(a.subs, 2);
  CHECK_STR(a.sub_size_str[0], "4G");
  CHECK_STR(a.sub_size_str[1], "1G");
  check_plan(8704 * MB, 4096, &a);
}

TEST(plan_rejects_bad_inputs) {
  hdl_alloc_t a;
  CHECK_EQ_INT(hdl_plan_alloc(0, 4096, &a), ERR_INVALID_ARG);
  CHECK_EQ_INT(hdl_plan_alloc(MB, 64, &a), ERR_HDL_PLAN);  /* no usable bucket */
  CHECK_EQ_INT(hdl_plan_alloc(MB, 0, &a), ERR_HDL_PLAN);
  CHECK_EQ_INT(hdl_plan_alloc(MB + 1, 4096, &a), ERR_INVALID_ARG); /* not 2 KiB aligned */
  /* Drive maximum above 4 GiB is clamped to the HDLFS 4 GiB limit. */
  CHECK_EQ_INT(hdl_plan_alloc(5000 * MB, 16384, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "4G");
}

/* Regression for the review finding: with a 1 MiB sub reserve the old
 * planner added an empty trailing 128M sub-partition when the last
 * chunk fell in ((b-1) MiB, b MiB - 2 KiB]. hdlfs reserves 4 sectors
 * (2 KiB) per sub, so one 4G sub holds it. */
TEST(plan_regression_no_empty_trailing_sub) {
  hdl_alloc_t a;
  uint64_t main_data = 4092 * MB;           /* 4G main minus 4 MiB */
  uint64_t bytes = main_data + 4095 * MB + MB / 2;
  CHECK_EQ_INT(hdl_plan_alloc(bytes, 4096, &a), ERR_OK);
  CHECK_EQ_INT(a.subs, 1);
  CHECK_STR(a.sub_size_str[0], "4G");
  check_plan(bytes, 4096, &a);
  /* Exactly fills a 4G sub (4096 MiB - 2 KiB). */
  bytes = main_data + 4096 * MB - S2K;
  CHECK_EQ_INT(hdl_plan_alloc(bytes, 4096, &a), ERR_OK);
  CHECK_EQ_INT(a.subs, 1);
  check_plan(bytes, 4096, &a);
  /* One sector more needs a second (smallest) sub. */
  bytes += S2K;
  CHECK_EQ_INT(hdl_plan_alloc(bytes, 4096, &a), ERR_OK);
  CHECK_EQ_INT(a.subs, 2);
  CHECK_STR(a.sub_size_str[1], "128M");
  check_plan(bytes, 4096, &a);
  /* 128 MiB - 2 KiB after the main fits a 128M sub (old planner: 256M). */
  bytes = main_data + 128 * MB - S2K;
  CHECK_EQ_INT(hdl_plan_alloc(bytes, 4096, &a), ERR_OK);
  CHECK_EQ_INT(a.subs, 1);
  CHECK_STR(a.sub_size_str[0], "128M");
}

TEST(plan_main_reserve_boundary) {
  hdl_alloc_t a;
  CHECK_EQ_INT(hdl_plan_alloc(124 * MB, 4096, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "128M");
  CHECK_EQ_INT(hdl_plan_alloc(124 * MB + S2K, 4096, &a), ERR_OK);
  CHECK_STR(a.main_size_str, "256M");
}
