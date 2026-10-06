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
            "title_id = SLUS-20312\r\n"
            "title_sub_id = 0\r\n"
            "release_date =\r\n"
            "developer_id = Unknown\r\n"
            "publisher_id = Unknown\r\n"
            "note = Installed with UDPFS Game Installer\r\n"
            "content_web = https://github.com/shefben/psx_udpfs_xmb_game_installer\r\n"
            "image_topviewflag = 0\r\n"
            "image_type = 0\r\n"
            "image_count = 1\r\n"
            "image_viewsec = 600\r\n"
            "copyright_viewflag = 0\r\n"
            "copyright_imgcount = 1\r\n"
            "genre = Unknown\r\n"
            "parental_lock = 1\r\n"
            "effective_date = 0\r\n"
            "expire_date = 0\r\n"
            "area = U\r\n"
            "violence_flag = 0\r\n"
            "content_type = 255\r\n"
            "content_subtype = 0"); /* no final line break, as BatchKit */
}

TEST(info_sys_matches_crlf_fixture) {
  /* Byte fixture: CRLF line endings, no final line break (BatchKit template). */
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

TEST(info_sys_retitle_keeps_every_other_line) {
  char buf[1024], out[1024], t[64];
  size_t n = xmb_game_info_sys(buf, sizeof(buf), "Gran Turismo 4", "SLUS_203.12");
  CHECK_EQ_INT(xmb_info_sys_get(buf, "title", t, sizeof(t)), 0);
  CHECK_STR(t, "Gran Turismo 4");
  CHECK_EQ_INT(xmb_info_sys_get(buf, "title_id", t, sizeof(t)), 0);
  CHECK_STR(t, "SLUS-20312");
  size_t m = xmb_info_sys_retitle(buf, "GT4 \r\nnote = x", out, sizeof(out));
  CHECK(m > 0);
  CHECK_EQ_INT(xmb_info_sys_get(out, "title", t, sizeof(t)), 0);
  CHECK_STR(t, "GT4 note = x"); /* control chars dropped: no injected line */
  CHECK(strstr(out, "title = GT4 note = x\r\ntitle_id = SLUS-20312\r\n") == out);
  CHECK_EQ_INT((int)(m - strlen("title = GT4 note = x\r\n")),
               (int)(n - strlen("title = Gran Turismo 4\r\n")));
  CHECK_EQ_INT(xmb_info_sys_retitle(buf, "   ", out, sizeof(out)), 0); /* empty title */
  CHECK_EQ_INT(xmb_info_sys_retitle("no title here\r\n", "X", out, sizeof(out)), 0);
  CHECK_EQ_INT(xmb_info_sys_get(buf, "missing", t, sizeof(t)), -1);
}

TEST(info_sys_game_info_fields) {
  xmb_game_info_t gi;
  const char *txt = "release_date=20011217\r\ndeveloper=Square Co. Ltd\ngenre=RPG\nnote=ignored\n"
                    "publisher=Square = Enix\n";
  CHECK_EQ_INT(xmb_game_info_parse(txt, &gi), 4);
  CHECK_STR(gi.release_date, "20011217");
  CHECK_STR(gi.publisher, "Square = Enix");
  char buf[1024];
  size_t n = xmb_game_info_sys_ex(buf, sizeof(buf), "Final Fantasy X", "SLUS_203.12", &gi, "20261005");
  CHECK(n > 0);
  CHECK(strstr(buf, "release_date = 20011217\r\n") != NULL);
  CHECK(strstr(buf, "developer_id = Square Co. Ltd\r\n") != NULL);
  CHECK(strstr(buf, "publisher_id = Square = Enix\r\n") != NULL);
  CHECK(strstr(buf, "genre = RPG\r\n") != NULL);
  /* bad date: left empty; no info: same bytes as the plain template */
  xmb_game_info_parse("release_date=2001\n", &gi);
  xmb_game_info_sys_ex(buf, sizeof(buf), "X", "SLUS_203.12", &gi, NULL);
  CHECK(strstr(buf, "release_date =\r\n") != NULL);
  char plain[1024];
  xmb_game_info_sys(plain, sizeof(plain), "X", "SLUS_203.12");
  xmb_game_info_sys_ex(buf, sizeof(buf), "X", "SLUS_203.12", NULL, NULL);
  CHECK_STR(buf, plain);
}

TEST(info_sys_unknown_region_still_renders) {
  char buf[2048];
  CHECK(xmb_game_info_sys(buf, sizeof(buf), "X", "ABXD_123.45") > 0);
  CHECK(strstr(buf, "title_id = ABXD-12345\r\n") != NULL);
  CHECK(strstr(buf, "area = X\r\n") != NULL);
}

TEST(info_sys_strips_control_chars) {
  char buf[2048];
  CHECK(xmb_render_info_sys(buf, sizeof(buf), "Evil\r\ntitle_id = X\t!",
                            "ID", NULL) > 0);
  CHECK(strncmp(buf, "title = Eviltitle_id = X!\r\n", 27) == 0);
  /* Exactly one "title_id" line. */
  CHECK(strstr(strstr(buf, "title_id = ID\r\n") + 1, "title_id =") == NULL);
}

TEST(info_sys_too_small_buffer) {
  char buf[32];
  CHECK_EQ_INT(xmb_render_info_sys(buf, sizeof(buf), "T", "I", NULL), 0);
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

TEST(area_letters_follow_batchkit) {
  CHECK_EQ_INT(xmb_area_letter("SLUS_203.12"), 'U');
  CHECK_EQ_INT(xmb_area_letter("SCUS_971.99"), 'U');
  CHECK_EQ_INT(xmb_area_letter("SLES_503.30"), 'E');
  CHECK_EQ_INT(xmb_area_letter("SLPM_650.51"), 'J');
  CHECK_EQ_INT(xmb_area_letter("SCKA_200.01"), 'K');
  CHECK_EQ_INT(xmb_area_letter("SCCS_400.01"), 'C');
  CHECK_EQ_INT(xmb_area_letter("SLAJ_250.01"), 'A');
  CHECK_EQ_INT(xmb_area_letter("ABXD_123.45"), 'X');
  CHECK_EQ_INT(xmb_area_letter(""), 'X');
}

TEST(icon_sys_hdd_format) {
  char buf[1024];
  size_t n = xmb_render_icon_sys(buf, sizeof(buf), "Gran Turismo 4\r\nx", "SLUS-20312");
  CHECK(n > 0 && n < PPAA_ICONSYS_MAX);
  CHECK_EQ_INT(n, strlen(buf));
  CHECK(strncmp(buf, "PS2X\ntitle0 = Gran Turismo 4x\ntitle1 = SLUS-20312\nbgcola = 64\n", 61) == 0);
  CHECK(strstr(buf, "lightcol2 = 18,18,49\nuninstallmes0 = This will delete the game.\n"
                    "uninstallmes1 =\nuninstallmes2 =\n") != NULL);
  CHECK(memchr(buf, '\r', n) == NULL);
  CHECK_EQ_INT(xmb_render_icon_sys(buf, 40, "T", "I"), 0);
}

TEST(hidden_system_cnf_is_hdlgi_res) {
  CHECK_STR(XMB_HIDDEN_SYSTEM_CNF,
            "BOOT2 = PATINFO\nVER = 1.00\nVMODE = NTSC\nHDDUNITPOWER = NICHDD\n");
}

static const uint8_t ICON[1000] = {0, 0, 1, 0, 1, 0, 0, 0, 7};
static const char ICONSYS[] = "PS2X\ntitle0 = A\ntitle1 = B\n";

TEST(ppaa_files_layout_matches_hdl_dump) {
  static uint8_t region[PPAA_OSD_MAX];
  ppaa_files_t f = {XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF), ICONSYS, strlen(ICONSYS),
                    ICON, sizeof(ICON)};
  CHECK_EQ_INT(ppaa_files_span(&f), 0x800 + 1024);
  memset(region, 0xEE, sizeof(region));
  CHECK_EQ_INT(ppaa_apply_files(region, sizeof(region), &f), ERR_OK);
  /* hdl_dump modify_header: 0x10 system.cnf, 0x18 icon.sys, 0x20 list
   * icon, 0x28 delete icon = list icon when there is no del.ico. */
  static const uint32_t desc[8] = {0x200, 0, 0x400, 0, 0x800, sizeof(ICON), 0x800, sizeof(ICON)};
  for (int i = 0; i < 8; i++) {
    uint32_t want = desc[i];
    if (i == 1)
      want = (uint32_t)strlen(XMB_SYSTEM_CNF);
    if (i == 3)
      want = (uint32_t)strlen(ICONSYS);
    const uint8_t *p = region + 0x10 + 4 * i;
    CHECK_EQ_INT(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24, want);
  }
  CHECK(memcmp(region + 0x400, ICONSYS, strlen(ICONSYS)) == 0);
  CHECK_EQ_INT(region[0x400 + strlen(ICONSYS)], 0);
  CHECK_EQ_INT(region[0x7FF], 0);
  CHECK(memcmp(region + 0x800, ICON, sizeof(ICON)) == 0);
  CHECK_EQ_INT(region[0x800 + sizeof(ICON)], 0); /* padded to 512 */
  CHECK_EQ_INT(region[0x800 + 1023], 0);
  CHECK_EQ_INT(region[0x800 + 1024], 0xEE); /* beyond the span: untouched */
  CHECK_EQ_INT(region[0x0C], 0xEE);          /* reserved bytes untouched */
  CHECK_EQ_INT(ppaa_verify_files(region, sizeof(region), &f), ERR_OK);
  CHECK(ppaa_has_icons(region, sizeof(region)));
  region[0x800 + 5] ^= 1;
  CHECK_EQ_INT(ppaa_verify_files(region, sizeof(region), &f), ERR_XMB_VERIFY);
}

TEST(ppaa_syscnf_only_has_no_icons) {
  static uint8_t region[PPAA_REGION_LEN];
  memset(region, 0, sizeof(region));
  ppaa_apply(region, sizeof(region), XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF));
  CHECK(!ppaa_has_icons(region, sizeof(region))); /* older channels: Repair offered */
}

TEST(ppaa_files_rejects_bad_sizes) {
  static uint8_t region[PPAA_OSD_MAX];
  ppaa_files_t f = {XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF), ICONSYS, 0, NULL, 0};
  CHECK_EQ_INT(ppaa_files_span(&f), 0);
  f.iconsys_len = PPAA_ICONSYS_MAX + 1;
  CHECK_EQ_INT(ppaa_apply_files(region, sizeof(region), &f), ERR_INVALID_ARG);
  f.iconsys_len = strlen(ICONSYS);
  f.icon = ICON;
  f.icon_len = PPAA_ICON_MAX + 1;
  CHECK_EQ_INT(ppaa_files_span(&f), 0);
  f.icon_len = sizeof(ICON);
  CHECK_EQ_INT(ppaa_apply_files(region, 0x800, &f), ERR_INVALID_ARG); /* too small */
}

TEST(release_date_falls_back_to_today) {
  char buf[1024];
  /* No database entry: today's date. */
  CHECK(xmb_game_info_sys_ex(buf, sizeof(buf), "X", "SLUS_203.12", NULL, "20261005") > 0);
  CHECK(strstr(buf, "release_date = 20261005\r\n") != NULL);
  /* A database date wins over today. */
  xmb_game_info_t gi;
  xmb_game_info_parse("release_date=20011217\n", &gi);
  xmb_game_info_sys_ex(buf, sizeof(buf), "X", "SLUS_203.12", &gi, "20261005");
  CHECK(strstr(buf, "release_date = 20011217\r\n") != NULL);
  /* Invalid database date: today. Invalid "today": empty. */
  xmb_game_info_parse("release_date=2001\n", &gi);
  xmb_game_info_sys_ex(buf, sizeof(buf), "X", "SLUS_203.12", &gi, "20261005");
  CHECK(strstr(buf, "release_date = 20261005\r\n") != NULL);
  xmb_game_info_sys_ex(buf, sizeof(buf), "X", "SLUS_203.12", NULL, "2026-1-5");
  CHECK(strstr(buf, "release_date =\r\n") != NULL);
  CHECK(xmb_render_info_sys(buf, sizeof(buf), "Installer", "UDPFS-INSTALLER", "20261005") > 0);
  CHECK(strstr(buf, "release_date = 20261005\r\n") != NULL);
}

TEST(date_str_validates) {
  char d[9];
  CHECK_EQ_INT(xmb_date_str(2026, 10, 5, d), 0);
  CHECK_STR(d, "20261005");
  CHECK_EQ_INT(xmb_date_str(2024, 2, 29, d), 0);
  CHECK_STR(d, "20240229");
  CHECK_EQ_INT(xmb_date_str(2025, 2, 29, d), -1);
  CHECK_EQ_INT(xmb_date_str(1999, 12, 31, d), -1); /* clock not set */
  CHECK_EQ_INT(xmb_date_str(2026, 13, 1, d), -1);
  CHECK_EQ_INT(xmb_date_str(2026, 4, 31, d), -1);
  CHECK_EQ_INT(xmb_date_str(2026, 1, 0, d), -1);
}

TEST(man_xml_default_template) {
  char buf[2048];
  size_t n = xmb_render_man_xml(buf, sizeof(buf), "Tom & Jerry <\"War\">\r\n");
  CHECK(n > 0);
  CHECK_EQ_INT(n, strlen(buf));
  CHECK(strncmp(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n", 40) == 0);
  CHECK(strstr(buf, "<TITLE id=\"TOP-TITLE\" label=\"Tom &amp; Jerry &lt;&quot;War&quot;&gt;\" />") != NULL);
  CHECK(strstr(buf, "<IMG id=\"bg\" src=\"./image/0.png\" />") != NULL);
  CHECK(strstr(buf, "src=\"./image/1.png\"") != NULL && strstr(buf, "src=\"./image/2.png\"") != NULL);
  CHECK(strstr(buf, "</MANUAL>\r\n") != NULL);
  CHECK_EQ_INT(xmb_render_man_xml(buf, 64, "T"), 0);
}