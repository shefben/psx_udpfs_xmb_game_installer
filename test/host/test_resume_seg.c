#include "../../src/resume_seg.h"
#include "test.h"

TEST(seg_roundtrip_and_order) {
  seg_list_t l = {0};
  CHECK_EQ_INT(seg_add(&l, 64ull << 20, 0x11111111u, 0xAAAAAAAAu), 0);
  CHECK_EQ_INT(seg_add(&l, 128ull << 20, 0x22222222u, 0xBBBBBBBBu), 0);
  CHECK_EQ_INT(seg_add(&l, 128ull << 20, 0x3, 0x3), -1); /* not forward */
  CHECK_EQ_INT(seg_add(&l, 100, 0x3, 0x3), -1);
  char buf[512];
  CHECK(seg_serialize(&l, buf, sizeof(buf)) > 0);
  CHECK_STR(buf, "67108864 11111111 aaaaaaaa\n134217728 22222222 bbbbbbbb\n");
  seg_list_t m;
  CHECK_EQ_INT(seg_parse(buf, &m), 2);
  CHECK_EQ_U64(m.s[1].bytes, 128ull << 20);
  CHECK_EQ_U64(m.s[1].cum_crc, 0x22222222u);
  CHECK_EQ_U64(seg_start(&m, 0), 0);
  CHECK_EQ_U64(seg_start(&m, 1), 64ull << 20);
  /* junk or a step backwards ends the list */
  CHECK_EQ_INT(seg_parse("10 1 2\nxx\n20 3 4\n", &m), 1);
  CHECK_EQ_INT(seg_parse("20 1 2\n10 3 4\n", &m), 1);
  CHECK_EQ_INT(seg_parse("", &m), 0);
}

TEST(seg_full_list_drops_oldest) {
  seg_list_t l = {0};
  for (int i = 1; i <= SEG_MAX + 3; i++)
    CHECK_EQ_INT(seg_add(&l, (uint64_t)i * 2048, (uint32_t)i, (uint32_t)i), 0);
  CHECK_EQ_INT(l.n, SEG_MAX);
  CHECK_EQ_U64(l.s[l.n - 1].bytes, (uint64_t)(SEG_MAX + 3) * 2048);
}

/* Fake HDD: segment i reads back correctly unless bad[i]. */
static int bad[8], reads;
static seg_list_t *cur;
static int fake_read(void *ctx, uint64_t start, uint64_t end, uint32_t *crc) {
  (void)ctx;
  reads++;
  for (int i = 0; i < cur->n; i++)
    if (seg_start(cur, i) == start && cur->s[i].bytes == end) {
      *crc = bad[i] ? ~cur->s[i].seg_crc : cur->s[i].seg_crc;
      return 0;
    }
  return -5;
}

TEST(seg_pick_resume_newest_good_segment) {
  seg_list_t l = {0};
  for (int i = 1; i <= 4; i++)
    seg_add(&l, (uint64_t)i << 20, (uint32_t)i, 0x100u + (uint32_t)i);
  cur = &l;
  int checked;
  memset(bad, 0, sizeof(bad));
  reads = 0;
  CHECK_EQ_INT(seg_pick_resume(&l, 8, fake_read, NULL, &checked), 3); /* all good */
  CHECK_EQ_INT(checked, 1);
  bad[3] = 1; /* last segment lost (cache not on disk) */
  CHECK_EQ_INT(seg_pick_resume(&l, 8, fake_read, NULL, &checked), 2);
  CHECK_EQ_INT(checked, 2);
  bad[2] = bad[1] = bad[0] = 1;
  CHECK_EQ_INT(seg_pick_resume(&l, 8, fake_read, NULL, &checked), -1);
  CHECK_EQ_INT(checked, 4);
  CHECK_EQ_INT(seg_pick_resume(&l, 2, fake_read, NULL, &checked), -1); /* limit */
  CHECK_EQ_INT(checked, 2);
  seg_list_t empty = {0};
  CHECK_EQ_INT(seg_pick_resume(&empty, 8, fake_read, NULL, &checked), -1);
}
