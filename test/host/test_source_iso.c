#include <stdlib.h>

#include "../../src/hdl_plan.h"
#include "../../src/iso9660.h"
#include "../../src/source.h"
#include "isobuild.h"
#include "test.h"

#define CNF_OK "BOOT2 = cdrom0:\\SLUS_203.12;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"
#define MB (1024ull * 1024ull)
#define GB (1024ull * MB)

static isob_t iso;

static inst_err_t probe(uint64_t total, uint32_t pvd_blocks, const char *cnf,
                        int udf, disc_hint_t hint, iso_info_t *info) {
  static GameSource src;
  static memsrc_t m;
  isob_build(&iso, "GT4                ", pvd_blocks, cnf, udf, 0);
  memsrc_init(&src, &m, total);
  memsrc_add(&m, 0, iso.img, sizeof(iso.img));
  return iso_probe(&src, hint, info);
}

/* ---- source classification ---- */

TEST(source_classify_names) {
  CHECK_EQ_INT(source_classify("Game.iso"), SRC_TYPE_ISO);
  CHECK_EQ_INT(source_classify("GAME.ISO"), SRC_TYPE_ISO);
  CHECK_EQ_INT(source_classify("Gran Turismo 4.zso.iso"), SRC_TYPE_ZSO);
  CHECK_EQ_INT(source_classify("X.ZSO.ISO"), SRC_TYPE_ZSO);
  CHECK_EQ_INT(source_classify("X.cso.iso"), SRC_TYPE_NONE);
  CHECK_EQ_INT(source_classify("X.chd.iso"), SRC_TYPE_NONE);
  CHECK_EQ_INT(source_classify("X.zso"), SRC_TYPE_ZSO_FILE); /* USB: decompressed on the PS2 */
  CHECK_EQ_INT(source_classify("X.bin"), SRC_TYPE_NONE);
  CHECK_EQ_INT(source_classify(".iso"), SRC_TYPE_NONE);
  CHECK_STR(source_type_label(SRC_TYPE_ZSO), "ZSO");
  CHECK_STR(source_type_label(SRC_TYPE_ISO), "ISO");
}

/* ---- memsrc + exact reads ---- */

TEST(source_read_exact_short_read_is_error) {
  GameSource src;
  memsrc_t m;
  uint8_t buf[100];
  memsrc_init(&src, &m, 50);
  CHECK_EQ_INT(source_read_exact(&src, buf, 100), ERR_SOURCE_READ);
}

TEST(source_read_exact_loops_partial_reads) {
  GameSource src;
  memsrc_t m;
  uint8_t data[300], buf[300];
  for (int i = 0; i < 300; i++)
    data[i] = (uint8_t)i;
  memsrc_init(&src, &m, 300);
  memsrc_add(&m, 0, data, 300);
  m.max_chunk = 7;
  CHECK_EQ_INT(source_read_exact(&src, buf, 300), ERR_OK);
  CHECK(memcmp(buf, data, 300) == 0);
}

TEST(source_read_error_distinct_from_eof) {
  GameSource src;
  memsrc_t m;
  uint8_t buf[16];
  memsrc_init(&src, &m, 1000);
  m.fail_after_reads = 1;
  CHECK_EQ_INT(source_read_exact(&src, buf, 16), ERR_SOURCE_READ);
  CHECK(src.last_rc < 0);
}

TEST(source_read_at_above_4gib) {
  GameSource src;
  memsrc_t m;
  uint8_t mark[4] = {1, 2, 3, 4}, buf[4];
  memsrc_init(&src, &m, 6 * GB);
  memsrc_add(&m, 5 * GB + 12345, mark, 4);
  CHECK_EQ_INT(source_read_at(&src, 5 * GB + 12345, buf, 4), ERR_OK);
  CHECK(memcmp(buf, mark, 4) == 0);
  CHECK_EQ_U64(source_size(&src), 6 * GB);
}

/* ---- ISO probe ---- */

TEST(iso_probe_cd_basic) {
  iso_info_t info;
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, CNF_OK, 0, DISC_HINT_NONE, &info),
               ERR_OK);
  CHECK_STR(info.volume_id, "GT4");
  CHECK_STR(info.boot_id, "SLUS_203.12");
  CHECK_STR(info.part_id, "SLUS-20312");
  CHECK_EQ_INT(info.sectors, 1000);
  CHECK_EQ_INT(info.pvd_blocks, 1000);
  CHECK_EQ_INT(info.disc_type, DISC_TYPE_CD);
  CHECK_EQ_INT(info.layer1_start, 0);
}

TEST(iso_probe_udf_means_dvd) {
  iso_info_t info;
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, CNF_OK, 1, DISC_HINT_NONE, &info),
               ERR_OK);
  CHECK(info.has_udf);
  CHECK_EQ_INT(info.disc_type, DISC_TYPE_DVD);
}

TEST(iso_probe_folder_hint_wins) {
  iso_info_t info;
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, CNF_OK, 1, DISC_HINT_CD, &info), ERR_OK);
  CHECK_EQ_INT(info.disc_type, DISC_TYPE_CD);
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, CNF_OK, 0, DISC_HINT_DVD, &info), ERR_OK);
  CHECK_EQ_INT(info.disc_type, DISC_TYPE_DVD);
}

TEST(iso_probe_large_without_udf_is_dvd) {
  iso_info_t info;
  uint32_t blocks = 500000; /* > 99-minute CD */
  CHECK_EQ_INT(probe(blocks * 2048ull, blocks, CNF_OK, 0, DISC_HINT_NONE, &info), ERR_OK);
  CHECK_EQ_INT(info.disc_type, DISC_TYPE_DVD);
}

TEST(iso_probe_rejects_non_iso) {
  GameSource src;
  memsrc_t m;
  iso_info_t info;
  memsrc_init(&src, &m, 64 * 2048);
  CHECK_EQ_INT(iso_probe(&src, DISC_HINT_NONE, &info), ERR_SOURCE_INVALID_ISO);
}

TEST(iso_probe_rejects_truncated_and_unaligned) {
  iso_info_t info;
  CHECK_EQ_INT(probe(999 * 2048ull, 1000, CNF_OK, 0, DISC_HINT_NONE, &info),
               ERR_SOURCE_INVALID_ISO);
  CHECK_EQ_INT(probe(1000 * 2048ull + 1, 1000, CNF_OK, 0, DISC_HINT_NONE, &info),
               ERR_SOURCE_INVALID_ISO);
}

TEST(iso_probe_short_source_is_read_error) {
  GameSource src;
  memsrc_t m;
  iso_info_t info;
  memsrc_init(&src, &m, 10 * 2048); /* PVD sector beyond EOF */
  CHECK_EQ_INT(iso_probe(&src, DISC_HINT_NONE, &info), ERR_SOURCE_READ);
}

TEST(iso_probe_missing_system_cnf) {
  iso_info_t info;
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, NULL, 0, DISC_HINT_NONE, &info),
               ERR_SOURCE_SYSTEM_CNF);
}

TEST(iso_probe_system_cnf_without_boot2) {
  iso_info_t info;
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, "BOOT = cdrom:\\PSX.EXE;1\r\n", 0,
                     DISC_HINT_NONE, &info),
               ERR_SOURCE_SYSTEM_CNF);
  CHECK_EQ_INT(probe(1000 * 2048ull, 1000, "BOOT2 = cdrom0:\\MAIN.ELF;1\n", 0,
                     DISC_HINT_NONE, &info),
               ERR_SOURCE_SYSTEM_CNF);
}

TEST(iso_probe_system_cnf_in_second_dir_sector) {
  static GameSource src;
  static memsrc_t m;
  iso_info_t info;
  isob_build(&iso, "PADDED", 1000, CNF_OK, 0, 60);
  memsrc_init(&src, &m, 1000 * 2048ull);
  memsrc_add(&m, 0, iso.img, sizeof(iso.img));
  CHECK_EQ_INT(iso_probe(&src, DISC_HINT_NONE, &info), ERR_OK);
  CHECK_STR(info.boot_id, "SLUS_203.12");
}

TEST(iso_probe_dvd9_layer_break) {
  static GameSource src;
  static memsrc_t m;
  iso_info_t info;
  uint32_t l0 = 2000000;
  uint64_t total = 7900ull * MB; /* > 4 GiB */
  isob_build(&iso, "DL", l0, CNF_OK, 1, 0);
  memsrc_init(&src, &m, total);
  memsrc_add(&m, 0, iso.img, sizeof(iso.img));
  /* layer-1 PVD lives at sector maxLBA (= layer1_start + 16) */
  memsrc_add(&m, (uint64_t)l0 * 2048, iso.img + 16 * 2048, 2048);
  CHECK_EQ_INT(iso_probe(&src, DISC_HINT_NONE, &info), ERR_OK);
  CHECK_EQ_INT(info.disc_type, DISC_TYPE_DVD);
  CHECK_EQ_INT(info.layer1_start, l0 - 16);
  CHECK_EQ_U64(info.source_size, total);
  CHECK_EQ_U64(info.sectors, total / 2048);
}

TEST(iso_probe_dvd5_has_no_layer_break) {
  iso_info_t info;
  uint32_t blocks = 2200000; /* 4.2 GiB, > 4 GiB but single layer */
  CHECK_EQ_INT(probe(blocks * 2048ull, blocks, CNF_OK, 1, DISC_HINT_NONE, &info), ERR_OK);
  CHECK_EQ_INT(info.layer1_start, 0);
  CHECK_EQ_U64(info.source_size, blocks * 2048ull);
}

TEST(iso_hint_from_paths) {
  CHECK_EQ_INT(iso_hint_from_path("udpfs:/DVD/Game.iso"), DISC_HINT_DVD);
  CHECK_EQ_INT(iso_hint_from_path("udpfs:/cd/Game.iso"), DISC_HINT_CD);
  CHECK_EQ_INT(iso_hint_from_path("udpfs:/PS2/DVD/sub/Game.iso"), DISC_HINT_DVD);
  CHECK_EQ_INT(iso_hint_from_path("udpfs:/DVD/CD/Game.iso"), DISC_HINT_CD);
  CHECK_EQ_INT(iso_hint_from_path("udpfs:/Games/Game.iso"), DISC_HINT_NONE);
  CHECK_EQ_INT(iso_hint_from_path("udpfs:/DVDs/Game.iso"), DISC_HINT_NONE);
}

