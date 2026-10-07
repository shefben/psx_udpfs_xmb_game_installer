/* source_cdvd.c over a fake drive (fake_cdvd.c): size of single- and
 * dual-layer discs, exact bytes through the read-ahead, and refusal of
 * any disc whose layers are unclear (a DVD-9 copied as one layer would
 * still pass its CRC check). */
#include <string.h>

#include "source.h"
#include "source_cdvd.h"
#include "test.h"

extern int fake_cd_type, fake_cd_violations, fake_cd_fail_lsn, fake_cd_reads;
extern int fake_cd_dual, fake_cd_dual_fail, fake_cd_busy_once;
extern uint32_t fake_cd_sectors, fake_cd_l0, fake_cd_l1, fake_cd_l1start;
void fake_cd_sector(uint32_t lsn, uint8_t *s);

static void disc(int type, uint32_t sectors, uint32_t l0, int dual, uint32_t l1) {
  fake_cd_type = type;
  fake_cd_sectors = sectors;
  fake_cd_l0 = l0;
  fake_cd_dual = dual;
  fake_cd_l1 = dual ? l1 : 0;
  fake_cd_l1start = dual ? l0 - 16 : 0;
  fake_cd_dual_fail = 0;
  fake_cd_fail_lsn = -1;
  fake_cd_busy_once = 0;
  fake_cd_violations = 0;
}

static int64_t drain(GameSource *s, uint64_t total, uint32_t blk) {
  static uint8_t buf[1 << 20], ref[2048];
  uint64_t pos = 0;
  while (pos < total) {
    uint32_t want = total - pos > blk ? blk : (uint32_t)(total - pos);
    const uint8_t *p;
    uint32_t got;
    if (source_next_block(s, buf, want, &p, &got) != ERR_OK)
      return -1;
    for (uint32_t i = 0; i < got; i += 2048) {
      fake_cd_sector((uint32_t)((pos + i) / 2048), ref);
      if (memcmp(p + i, ref, 2048))
        return -1;
    }
    pos += got;
  }
  return (int64_t)pos;
}

TEST(cdvd_single_layer_size_and_bytes) {
  static GameSource s;
  static cdvd_src_t c;
  disc(0x14, 3000, 2900, 0, 0); /* the volume, not the readable area, sets the size */
  source_cdvd_init(&s, &c);
  CHECK_EQ_INT(source_open(&s, CDVD_PATH), ERR_OK);
  CHECK_EQ_U64(source_size(&s), 2900ull * 2048);
  CHECK(c.dvd);
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  CHECK_EQ_U64(drain(&s, 2900ull * 2048, 128 * 1024), 2900ull * 2048);
  source_close(&s);
  CHECK_EQ_INT(fake_cd_violations, 0);
}

TEST(cdvd_dual_layer_joins_both_volumes) {
  static GameSource s;
  static cdvd_src_t c;
  disc(0x14, 2000, 1000, 1, 800); /* layer 1 starts at 984, its PVD at 1000 */
  source_cdvd_init(&s, &c);
  CHECK_EQ_INT(source_open(&s, CDVD_PATH), ERR_OK);
  CHECK_EQ_U64(source_size(&s), (984ull + 800) * 2048);
  CHECK_EQ_U64(drain(&s, (984ull + 800) * 2048, 96 * 1024), (984ull + 800) * 2048);
  source_close(&s);
}

TEST(cdvd_unclear_layers_refuse_the_disc) {
  static GameSource s;
  static cdvd_src_t c;
  /* The drive does not answer the layer question. */
  disc(0x14, 2000, 1000, 1, 800);
  fake_cd_dual_fail = 1;
  source_cdvd_init(&s, &c);
  CHECK(source_open(&s, CDVD_PATH) != ERR_OK);
  /* Dual layer, but layer 1's PVD cannot be read. */
  disc(0x14, 2000, 1000, 1, 800);
  fake_cd_fail_lsn = 1000;
  source_cdvd_init(&s, &c);
  CHECK(source_open(&s, CDVD_PATH) != ERR_OK);
  /* Dual layer, layer 1 not where layer 0's volume ends. */
  disc(0x14, 2000, 1000, 1, 800);
  fake_cd_l1start = 900;
  source_cdvd_init(&s, &c);
  CHECK(source_open(&s, CDVD_PATH) != ERR_OK);
  /* Dual layer, no PVD at layer 1. */
  disc(0x14, 2000, 1000, 1, 800);
  fake_cd_l1 = 0;
  source_cdvd_init(&s, &c);
  CHECK(source_open(&s, CDVD_PATH) != ERR_OK);
}

TEST(cdvd_read_error_is_reported_then_retried) {
  static GameSource s;
  static cdvd_src_t c;
  disc(0x12, 1200, 1200, 0, 0); /* PS2 CD: no layer question */
  source_cdvd_init(&s, &c);
  CHECK_EQ_INT(source_open(&s, CDVD_PATH), ERR_OK);
  CHECK(!c.dvd);
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  fake_cd_fail_lsn = 700;
  static uint8_t buf[1 << 17], ref[2048];
  uint64_t pos = 0, total = 1200ull * 2048;
  int errors = 0, ok = 1;
  while (pos < total && errors < 5) {
    const uint8_t *p;
    uint32_t got;
    uint32_t want = total - pos > sizeof(buf) ? sizeof(buf) : (uint32_t)(total - pos);
    if (pos == 300 * 2048)
      fake_cd_busy_once = 1; /* the drive refuses one read command */
    if (source_next_block(&s, buf, want, &p, &got) != ERR_OK) {
      errors++;
      continue;
    }
    for (uint32_t i = 0; i < got; i += 2048) {
      fake_cd_sector((uint32_t)((pos + i) / 2048), ref);
      ok &= !memcmp(p + i, ref, 2048);
    }
    pos += got;
  }
  CHECK(ok);
  CHECK(errors >= 1 && errors <= 2);
  CHECK_EQ_U64(pos, total);
  source_close(&s);
  CHECK_EQ_INT(fake_cd_violations, 0);
}

TEST(cdvd_refuses_non_ps2_discs) {
  static GameSource s;
  static cdvd_src_t c;
  disc(0x10, 1000, 1000, 0, 0); /* PS1 */
  source_cdvd_init(&s, &c);
  CHECK(source_open(&s, CDVD_PATH) != ERR_OK);
  disc(0xFE, 1000, 1000, 0, 0); /* DVD video */
  source_cdvd_init(&s, &c);
  CHECK(source_open(&s, CDVD_PATH) != ERR_OK);
  disc(0x14, 1000, 1000, 0, 0);
}
