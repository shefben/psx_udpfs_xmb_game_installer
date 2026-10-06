#include "../../src/hdl_header.h"
#include "../../src/util.h"
#include "test.h"

static iso_info_t sample_iso(void) {
  iso_info_t i;
  memset(&i, 0, sizeof(i));
  strcpy(i.volume_id, "GT4");
  strcpy(i.boot_id, "SLUS_203.12");
  strcpy(i.part_id, "SLUS-20312");
  i.sectors = 3000000;
  i.disc_type = DISC_TYPE_DVD;
  i.layer1_start = 1234;
  return i;
}

TEST(format_args_layout_and_fill) {
  struct HDLFS_FormatArgs a;
  iso_info_t iso = sample_iso();
  hdl_format_args_build(&a, &iso, "Gran Turismo 4");
  const uint8_t *b = (const uint8_t *)&a;
  CHECK_EQ_INT(b[0], 0);
  CHECK_EQ_INT(b[1], 0x14);
  CHECK_EQ_INT(get_u32le(b + 4), 3000000);
  CHECK_EQ_INT(get_u32le(b + 8), 1234);
  CHECK_STR((const char *)b + 12, "Gran Turismo 4");
  CHECK_STR((const char *)b + 172, "SLUS_203.12");
}

TEST(format_args_title_truncated_terminated) {
  struct HDLFS_FormatArgs a;
  iso_info_t iso = sample_iso();
  char longt[300];
  memset(longt, 'x', sizeof(longt) - 1);
  longt[299] = 0;
  hdl_format_args_build(&a, &iso, longt);
  CHECK_EQ_INT(strlen(a.GameTitle), HDLFS_GAME_TITLE_LEN - 1);
}

static void make_header(uint8_t *buf, int parts, const uint32_t *sizes) {
  memset(buf, 0, HDL_HEADER_SIZE);
  put_u32le(buf + 0, HDL_INFO_MAGIC);
  buf[6] = 1;
  strcpy((char *)buf + 8, "Gran Turismo 4");
  strcpy((char *)buf + 0xAC, "SLUS_203.12");
  put_u32le(buf + 0xE8, 1234);
  put_u32le(buf + 0xEC, 0x14);
  put_u32le(buf + 0xF0, (uint32_t)parts);
  for (int i = 0; i < parts; i++)
    put_u32le(buf + 0xF4 + i * 12 + 8, sizes[i]);
}

TEST(hdl_header_parse_offsets) {
  uint8_t buf[HDL_HEADER_SIZE];
  uint32_t sizes[2] = {2095104u * 2048u, 1000u * 2048u};
  make_header(buf, 2, sizes);
  hdl_header_info_t h;
  CHECK_EQ_INT(hdl_header_parse(buf, &h), 0);
  CHECK_STR(h.title, "Gran Turismo 4");
  CHECK_STR(h.startup, "SLUS_203.12");
  CHECK_EQ_INT(h.version, 1);
  CHECK_EQ_INT(h.disc_type, 0x14);
  CHECK_EQ_INT(h.layer1_start, 1234);
  CHECK_EQ_INT(h.num_partitions, 2);
  CHECK_EQ_U64(h.data_bytes, (2095104ull + 1000ull) * 2048ull);
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x14, 1234, 2,
                                (2095104ull + 1000ull) * 2048ull),
               ERR_OK);
}

TEST(hdl_header_check_rejects) {
  uint8_t buf[HDL_HEADER_SIZE];
  uint32_t sizes[1] = {1000u * 2048u};
  hdl_header_info_t h;
  make_header(buf, 1, sizes);
  hdl_header_parse(buf, &h);
  uint64_t n = 1000ull * 2048ull;
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x14, 1234, 1, n), ERR_OK);
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.13", 0x14, 1234, 1, n), ERR_HDL_VERIFY);
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x12, 1234, 1, n), ERR_HDL_VERIFY);
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x14, 0, 1, n), ERR_HDL_VERIFY);
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x14, 1234, 2, n), ERR_HDL_VERIFY);
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x14, 1234, 1, n + 2048), ERR_HDL_VERIFY);
  h.title[0] = 0;
  CHECK_EQ_INT(hdl_header_check(&h, "SLUS_203.12", 0x14, 1234, 1, n), ERR_HDL_VERIFY);
  put_u32le(buf, 0x12345678);
  CHECK_EQ_INT(hdl_header_parse(buf, &h), -1);
  make_header(buf, 1, sizes);
  put_u32le(buf + 0xF0, 66);
  CHECK_EQ_INT(hdl_header_parse(buf, &h), -1);
}

TEST(kelf_sanity) {
  static uint8_t buf[4096];
  memset(buf, 0, sizeof(buf));
  buf[0] = 1;
  buf[3] = 1;
  CHECK(kelf_looks_valid(buf, sizeof(buf)));
  CHECK(!kelf_looks_valid(buf, 100));
  CHECK(!kelf_looks_valid(NULL, 0));
  memcpy(buf, "\x7f" "ELF", 4);
  CHECK(!kelf_looks_valid(buf, sizeof(buf)));
}

TEST(png_sanity) {
  static uint8_t png[64];
  memset(png, 0, sizeof(png));
  memcpy(png, "\x89PNG\r\n\x1a\n", 8);
  put_u32le(png + 8, 0); /* length is big-endian; set below */
  png[8] = 0; png[9] = 0; png[10] = 0; png[11] = 13;
  memcpy(png + 12, "IHDR", 4);
  png[18] = 0; png[19] = 74;   /* width 74 (BE) */
  png[22] = 0; png[23] = 108;  /* height 108 (BE) */
  CHECK(png_basic_valid(png, sizeof(png)));
  CHECK(png_is_size(png, sizeof(png), 74, 108));
  CHECK(!png_is_size(png, sizeof(png), 140, 200));
  CHECK(!png_is_size(png, 20, 74, 108));
  CHECK(!png_basic_valid(png, 20));
  png[19] = 0; /* width 0 */
  CHECK(!png_basic_valid(png, sizeof(png)));
  png[19] = 74;
  png[13] = 'X';
  CHECK(!png_basic_valid(png, sizeof(png)));
  png[13] = 'H';
  png[0] = 0;
  CHECK(!png_basic_valid(png, sizeof(png)));
}

TEST(default_title_rules) {
  iso_info_t iso = sample_iso();
  char t[128];
  default_display_title(&iso, "udpfs:/DVD/Gran Turismo 4.zso.iso", t, sizeof(t));
  CHECK_STR(t, "GT4");
  strcpy(iso.volume_id, "SLUS_203.12");
  default_display_title(&iso, "udpfs:/DVD/Gran Turismo 4.zso.iso", t, sizeof(t));
  CHECK_STR(t, "Gran Turismo 4");
  strcpy(iso.volume_id, "SLUS-20312");
  default_display_title(&iso, "udpfs:/Gran Turismo 4.ISO", t, sizeof(t));
  CHECK_STR(t, "Gran Turismo 4");
  iso.volume_id[0] = 0;
  default_display_title(&iso, "udpfs:/x/ICO.iso", t, sizeof(t));
  CHECK_STR(t, "ICO");
}
