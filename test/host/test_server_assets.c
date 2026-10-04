#include "../../src/server_assets.h"
#include "../../src/sha256.h"
#include "test.h"

static uint8_t kelf[4096];

TEST(launcher_copy_requires_hash_size_and_kelf) {
  memset(kelf, 'K', sizeof(kelf));
  char hex[65];
  sha256_hex(kelf, sizeof(kelf), hex);
  CHECK(launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf)));
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf) - 1)); /* size mismatch */
  kelf[100] ^= 1;
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf))); /* stale/corrupt */
  kelf[100] ^= 1;
  memcpy(kelf, "\x7f" "ELF", 4);
  sha256_hex(kelf, sizeof(kelf), hex);
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf))); /* plain ELF */
  CHECK(!launcher_copy_valid(NULL, 0, hex, 0));
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), "", sizeof(kelf)));
}

TEST(opl_cfg_never_overwrites) {
  CHECK_EQ_INT(opl_cfg_decide(1, 0), OPL_CFG_COPY);
  CHECK_EQ_INT(opl_cfg_decide(1, 1), OPL_CFG_KEEP);
  CHECK_EQ_INT(opl_cfg_decide(0, 0), OPL_CFG_NONE);
  CHECK_EQ_INT(opl_cfg_decide(0, 1), OPL_CFG_NONE);
}

TEST(opl_cfg_dir_follows_opl_prefix_rule) {
  CHECK_STR(opl_cfg_dir("+OPL"), "pfs1:CFG/");
  CHECK_STR(opl_cfg_dir("__common"), "pfs1:OPL/CFG/");
  CHECK_STR(opl_cfg_dir("PP.OPL"), "pfs1:OPL/CFG/");
}

TEST(blob_matches_requires_exact_size_and_hash) {
  static uint8_t elf[3000];
  memset(elf, 'O', sizeof(elf));
  memcpy(elf, "\x7f" "ELF", 4);
  char hex[65];
  sha256_hex(elf, sizeof(elf), hex);
  CHECK(blob_matches(elf, sizeof(elf), hex, sizeof(elf)));
  CHECK(!blob_matches(elf, sizeof(elf) - 1, hex, sizeof(elf)));
  elf[2000] ^= 1;
  CHECK(!blob_matches(elf, sizeof(elf), hex, sizeof(elf)));
  CHECK(!blob_matches(NULL, 0, hex, 0));
}

TEST(opl_install_decision) {
  /* conf malformed: never touch anything */
  CHECK_EQ_INT(opl_install_decide(1, 1, 1, 0), OPL_INSTALL_CANNOT);
  /* runtime present */
  CHECK_EQ_INT(opl_install_decide(0, 0, 1, 1), OPL_INSTALL_PRESENT);
  CHECK_EQ_INT(opl_install_decide(0, 1, 1, 1), OPL_INSTALL_PRESENT);
  /* default +OPL missing: create it (as OPL itself would) */
  CHECK_EQ_INT(opl_install_decide(0, 0, 0, 0), OPL_INSTALL_CREATE_PARTITION);
  /* partition exists (default or configured), ELF missing: add the ELF */
  CHECK_EQ_INT(opl_install_decide(0, 0, 1, 0), OPL_INSTALL_ADD_ELF);
  CHECK_EQ_INT(opl_install_decide(0, 1, 1, 0), OPL_INSTALL_ADD_ELF);
  /* conf_hdd.cfg names a partition that does not exist: do not guess */
  CHECK_EQ_INT(opl_install_decide(0, 1, 0, 0), OPL_INSTALL_CANNOT);
}
