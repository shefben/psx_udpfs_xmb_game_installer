#include "test.h"
#include "source_disc.h"
#include "iso9660.h"
#include "pops.h"
#include "isobuild.h"

static disc_info_t media;
static uint8_t image[64 * 2048];
static int read_error, reads;
static int prepare(void *ctx, disc_info_t *info) {
  (void)ctx;
  *info = media;
  return 0;
}
static int read_sectors(void *ctx, uint32_t lba, uint32_t n, int raw, void *out) {
  (void)ctx;
  reads++;
  if (read_error) return read_error;
  if (lba + n > media.sectors) return -5;
  if (!raw) memcpy(out, image + lba * 2048, n * 2048);
  else {
    uint8_t *p = out;
    for (uint32_t i = 0; i < n; i++, p += 2352) {
      memset(p, 0, 2352);
      memset(p + 1, 0xff, 10);
      p[15] = 2;
      memcpy(p + 24, image + (lba + i) * 2048, 2048);
    }
  }
  return 0;
}
static void setup(GameSource *s, disc_src_t *d, int ps1) {
  memset(&media, 0, sizeof(media));
  media.ps1 = ps1;
  media.sectors = 64;
  media.toc[0] = media.toc[10] = media.toc[20] = media.toc[30] = 0x41;
  media.toc[2] = 0xa0; media.toc[7] = 1; media.toc[8] = 0x20;
  media.toc[12] = 0xa1; media.toc[17] = 1;
  media.toc[22] = 0xa2; media.toc[28] = 2; media.toc[29] = 0x64;
  media.toc[32] = 1; media.toc[38] = 2;
  memset(image, 0x5a, sizeof(image));
  read_error = reads = 0;
  source_disc_init(s, d, prepare, read_sectors, NULL);
}
TEST(disc_iso_unaligned_reads_seek_and_eof) {
  GameSource s; disc_src_t d;
  setup(&s, &d, 0);
  CHECK_EQ_INT(source_open(&s, "disc:/CD/game.iso"), ERR_OK);
  CHECK_EQ_U64(source_size(&s), 131072);
  uint8_t b[3000];
  CHECK_EQ_INT(source_read_at(&s, 2047, b, sizeof(b)), ERR_OK);
  CHECK(!memcmp(b, image + 2047, sizeof(b)));
  CHECK_EQ_U64(s.ops->seek(&s, -3, SRC_SEEK_END), 131069);
  CHECK_EQ_INT(s.ops->read(&s, b, 20), 3);
  CHECK_EQ_INT(s.ops->read(&s, b, 20), 0);
  CHECK(s.ops->seek(&s, -1, SRC_SEEK_SET) < 0);
  source_close(&s);
}
TEST(disc_vcd_header_and_raw_sectors) {
  GameSource s; disc_src_t d;
  setup(&s, &d, 1);
  CHECK_EQ_INT(source_open(&s, "disc:/PS1/game.VCD"), ERR_OK);
  CHECK_EQ_U64(source_size(&s), 1048576 + 64 * 2352);
  uint8_t b[40];
  CHECK_EQ_INT(source_read_at(&s, 0, b, 40), ERR_OK);
  CHECK_EQ_INT(b[0], 0x41); CHECK_EQ_INT(b[2], 0xa0);
  CHECK_EQ_INT(b[8], 0x20); CHECK_EQ_INT(b[32], 1);
  CHECK_EQ_INT(source_read_at(&s, 1032, b, 8), ERR_OK);
  CHECK_EQ_INT(b[0], 64); CHECK_EQ_INT(b[4], 64);
  CHECK_EQ_INT(source_read_at(&s, 1048575, b, 40), ERR_OK);
  CHECK_EQ_INT(b[0], 0); CHECK_EQ_INT(b[1], 0); CHECK_EQ_INT(b[2], 255);
  CHECK_EQ_INT(b[16], 2); CHECK_EQ_INT(b[25], 0x5a);
  CHECK_EQ_INT(source_read_at(&s, 1048576 + 16 * 2352 + 24, b, 40), ERR_OK);
  CHECK(!memcmp(b, image + 16 * 2048, 40));
  source_close(&s);
}
TEST(disc_source_rejects_wrong_media_and_reports_read_error) {
  GameSource s; disc_src_t d;
  setup(&s, &d, 0);
  CHECK(source_open(&s, "disc:/PS1/game.VCD") != ERR_OK);
  CHECK_EQ_INT(source_open(&s, "disc:/CD/game.iso"), ERR_OK);
  read_error = -77;
  uint8_t b[2048];
  CHECK_EQ_INT(source_read_exact(&s, b, sizeof(b)), ERR_SOURCE_READ);
  CHECK_EQ_INT(s.last_rc, -77);
  source_close(&s);
}
TEST(disc_vcd_rejects_invalid_toc_before_copy) {
  GameSource s; disc_src_t d;
  setup(&s, &d, 1);
  media.toc[17] = 0x99; /* claims tracks that have no entries */
  CHECK(source_open(&s, "disc:/PS1/game.VCD") != ERR_OK);
  CHECK_EQ_INT(reads, 0);
}

TEST(disc_images_are_readable_by_real_ps1_and_ps2_probes) {
  GameSource s; disc_src_t d;
  static isob_t b;
  setup(&s, &d, 0);
  isob_build(&b, "PS2 GAME", 64, "BOOT2 = cdrom0:\\SLUS_203.12;1\r\n", 0, 0);
  memcpy(image, b.img, sizeof(b.img));
  CHECK_EQ_INT(source_open(&s, "disc:/CD/game.iso"), ERR_OK);
  iso_info_t iso;
  CHECK_EQ_INT(iso_probe(&s, DISC_HINT_CD, &iso), ERR_OK);
  CHECK_STR(iso.boot_id, "SLUS_203.12");
  CHECK_EQ_INT(iso.sectors, 64);
  source_close(&s);
  setup(&s, &d, 1);
  isob_build(&b, "PS1 GAME", 64, "BOOT = cdrom:\\SLUS_005.94;1\r\n", 0, 0);
  memcpy(image, b.img, sizeof(b.img));
  CHECK_EQ_INT(source_open(&s, "disc:/PS1/game.VCD"), ERR_OK);
  vcd_info_t vcd;
  CHECK_EQ_INT(vcd_probe(&s, &vcd), ERR_OK);
  CHECK_STR(vcd.boot_id, "SLUS_005.94");
  CHECK_EQ_INT(vcd.sectors, 64);
  source_close(&s);
}
