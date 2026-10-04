#include <stdio.h>
#include <stdlib.h>

#include "../../src/apa_osd_header.h"
#include "../../src/xmb_text.h"
#include "test.h"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "../fixtures"
#endif

static size_t load(const char *name, uint8_t *buf, size_t max) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  size_t n = fread(buf, 1, max, f);
  fclose(f);
  return n;
}

TEST(system_cnf_exact_bytes) {
  CHECK_STR(XMB_SYSTEM_CNF, "BOOT2 = pfs:/EXECUTE.KELF\nVER = 1.00\n"
                            "VMODE = NTSC\nHDDUNITPOWER = NICHDD\n");
  uint8_t fx[256];
  size_t n = load("system_cnf.bin", fx, sizeof(fx));
  CHECK_EQ_INT(n, strlen(XMB_SYSTEM_CNF));
  CHECK(memcmp(fx, XMB_SYSTEM_CNF, n) == 0);
}

TEST(info_sys_game_template) {
  char buf[2048];
  size_t n = xmb_game_info_sys(buf, sizeof(buf), "Gran Turismo 4", "SLUS_203.12");
  CHECK(n > 0);
  CHECK_EQ_INT(n, strlen(buf));
  CHECK_STR(buf,
            "title = Gran Turismo 4\r\n"
            "title_id = SLUS-20312 (NTSC-U)\r\n"
            "title_sub_id = 0\r\n"
            "release_date =\r\n"
            "developer_id =\r\n"
            "publisher_id =\r\n"
            "note =\r\n"
            "content_web =\r\n"
            "image_topviewflag = 0\r\n"
            "image_type = 0\r\n"
            "image_count = 1\r\n"
            "image_viewsec = 600\r\n"
            "copyright_viewflag = 0\r\n"
            "copyright_imgcount = 0\r\n"
            "genre =\r\n"
            "parental_lock = 1\r\n"
            "effective_date = 0\r\n"
            "expire_date = 0\r\n"
            "area = J\r\n"
            "violence_flag = 0\r\n"
            "content_type = 255\r\n"
            "content_subtype = 0\r\n");
}

TEST(info_sys_matches_crlf_fixture) {
  /* Byte fixture: CRLF line endings, as PSX-XMB-Manager writes info.sys. */
  char buf[2048];
  uint8_t fx[2048];
  size_t n = xmb_game_info_sys(buf, sizeof(buf), "Gran Turismo 4", "SLUS_203.12");
  size_t f = load("info_sys_gt4.bin", fx, sizeof(fx));
  CHECK_EQ_INT(n, f);
  CHECK(f > 0 && memcmp(buf, fx, f) == 0);
  /* Every line ends in CRLF; there is no bare LF. */
  for (size_t i = 0; i < f; i++)
    if (fx[i] == '\n')
      CHECK(i > 0 && fx[i - 1] == '\r');
}

TEST(system_cnf_fixture_is_lf_only) {
  uint8_t fx[256];
  size_t n = load("system_cnf.bin", fx, sizeof(fx));
  CHECK(n > 0);
  CHECK(memchr(fx, '\r', n) == NULL);
}

TEST(info_sys_unknown_region_still_renders) {
  char buf[2048];
  CHECK(xmb_game_info_sys(buf, sizeof(buf), "X", "ABCD_123.45") > 0);
  CHECK(strstr(buf, "title_id = ABCD-12345 (UNKNOWN)\r\n") != NULL);
}

TEST(info_sys_strips_control_chars) {
  char buf[2048];
  CHECK(xmb_render_info_sys(buf, sizeof(buf), "Evil\r\ntitle_id = X\t!",
                            "ID") > 0);
  CHECK(strncmp(buf, "title = Eviltitle_id = X!\r\n", 27) == 0);
  /* Exactly one "title_id" line. */
  CHECK(strstr(strstr(buf, "title_id = ID\r\n") + 1, "title_id =") == NULL);
}

TEST(info_sys_too_small_buffer) {
  char buf[32];
  CHECK_EQ_INT(xmb_render_info_sys(buf, sizeof(buf), "T", "I"), 0);
}

TEST(sanitize_value_trims_and_keeps_utf8) {
  char out[64];
  xmb_sanitize_value("  \xe3\x83\x86 x\x7f  ", out, sizeof(out));
  CHECK_STR(out, "\xe3\x83\x86 x");
}

TEST(ppaa_matches_hdldump_fixture) {
  static uint8_t fx[2048], region[2048];
  size_t n = load("ppaa_hdldump.bin", fx, sizeof(fx));
  CHECK_EQ_INT(n, 2048);
  memset(region, 0, sizeof(region));
  CHECK_EQ_INT(ppaa_apply(region, sizeof(region), XMB_SYSTEM_CNF,
                          strlen(XMB_SYSTEM_CNF)),
               ERR_OK);
  CHECK(memcmp(region, fx, sizeof(fx)) == 0);
  CHECK_EQ_INT(ppaa_verify(fx, sizeof(fx), XMB_SYSTEM_CNF,
                           strlen(XMB_SYSTEM_CNF)),
               ERR_OK);
}

TEST(ppaa_preserves_unrelated_bytes) {
  static uint8_t region[PPAA_REGION_LEN], before[PPAA_REGION_LEN];
  for (int i = 0; i < PPAA_REGION_LEN; i++)
    region[i] = (uint8_t)(0xA5 ^ i);
  memcpy(before, region, sizeof(region));
  size_t len = strlen(XMB_SYSTEM_CNF);
  CHECK_EQ_INT(ppaa_apply(region, sizeof(region), XMB_SYSTEM_CNF, len), ERR_OK);
  for (int i = 0; i < PPAA_REGION_LEN; i++) {
    int touched = i < PPAA_MAGIC_LEN || (i >= 0x10 && i < 0x18) || i >= 0x200;
    if (!touched && region[i] != before[i]) {
      CHECK(!"unrelated byte modified");
      break;
    }
  }
  CHECK(memcmp(region, PPAA_MAGIC, PPAA_MAGIC_LEN) == 0);
  CHECK(memcmp(region + 0x200, XMB_SYSTEM_CNF, len) == 0);
  for (size_t i = 0x200 + len; i < 0x400; i++)
    CHECK_EQ_INT(region[i], 0);
}

TEST(ppaa_verify_detects_mismatch) {
  static uint8_t region[PPAA_REGION_LEN];
  size_t len = strlen(XMB_SYSTEM_CNF);
  memset(region, 0, sizeof(region));
  CHECK_EQ_INT(ppaa_verify(region, sizeof(region), XMB_SYSTEM_CNF, len),
               ERR_XMB_VERIFY);
  ppaa_apply(region, sizeof(region), XMB_SYSTEM_CNF, len);
  region[0x205] ^= 1;
  CHECK_EQ_INT(ppaa_verify(region, sizeof(region), XMB_SYSTEM_CNF, len),
               ERR_XMB_VERIFY);
  region[0x205] ^= 1;
  region[0x14] = 0x47;
  CHECK_EQ_INT(ppaa_verify(region, sizeof(region), XMB_SYSTEM_CNF, len),
               ERR_XMB_VERIFY);
}

TEST(ppaa_rejects_bad_args) {
  static uint8_t region[PPAA_REGION_LEN];
  static char big[PPAA_SYSCNF_MAX + 2];
  memset(big, 'a', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  CHECK_EQ_INT(ppaa_apply(region, sizeof(region), big, strlen(big)), ERR_INVALID_ARG);
  CHECK_EQ_INT(ppaa_apply(region, sizeof(region), "", 0), ERR_INVALID_ARG);
  CHECK_EQ_INT(ppaa_apply(region, 0x3FF, "x", 1), ERR_INVALID_ARG);
}

TEST(ppaa_read_syscnf_roundtrip) {
  static uint8_t region[PPAA_REGION_LEN];
  char out[600];
  memset(region, 0, sizeof(region));
  CHECK_EQ_INT(ppaa_read_syscnf(region, sizeof(region), out, sizeof(out)), -1);
  ppaa_apply(region, sizeof(region), XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF));
  CHECK_EQ_INT(ppaa_read_syscnf(region, sizeof(region), out, sizeof(out)),
               strlen(XMB_SYSTEM_CNF));
  CHECK_STR(out, XMB_SYSTEM_CNF);
}
