/* Partition deletion policy: the installer whitelist (src/partname.c)
 * and the driver-side rule compiled into ps2hdd-hdl.irx
 * (tools/driver/remove_policy.h, included unchanged). */
#include "../../src/partname.h"
#include "../../tools/driver/remove_policy.h"
#include "test.h"

#define T_HDL 0x1337
#define T_PFS 0x0100
#define T_EXT2 0x0083

/* ---- driver rule ---- */

static int drv_allows(const char *id, unsigned type) {
  return apaRemoveNameAllowed(id) && apaRemoveTypeAllowed(id, type);
}

TEST(driver_policy_hidden_hdl_game_deletable) {
  CHECK(drv_allows("__.SLUS-20312..GRAN_TURISMO_4", T_HDL));
  CHECK(drv_allows("__.SCES-50362..X", T_HDL));
}

TEST(driver_policy_system_partitions_protected) {
  CHECK(!apaRemoveNameAllowed("__mbr"));
  CHECK(!apaRemoveNameAllowed("__common"));
  CHECK(!apaRemoveNameAllowed("__system"));
  CHECK(!apaRemoveNameAllowed("__sysconf"));
  CHECK(!apaRemoveNameAllowed("__net"));
  CHECK(!apaRemoveNameAllowed("__contents"));
  CHECK(!apaRemoveNameAllowed("__xcontents"));
  CHECK(!apaRemoveNameAllowed("__extend"));
  CHECK(!apaRemoveNameAllowed("__empty"));
  CHECK(!drv_allows("__common", T_PFS));
}

TEST(driver_policy_dunder_dot_needs_hdl_type) {
  /* Not "everything starting with __" -- a "__." name of another type
   * (e.g. Linux partitions "__.linux.N", or a PFS partition someone
   * named "__.x") stays protected. */
  CHECK(!drv_allows("__.linux.1", T_EXT2));
  CHECK(!drv_allows("__.SLUS-20312..GAME", T_PFS));
  CHECK(!drv_allows("__.anything", 0x0001));
}

TEST(driver_policy_other_names_unchanged) {
  /* Upstream behaviour for non-"__" names is untouched. */
  CHECK(drv_allows("PP.SLUS-20312..GAME", T_PFS));
  CHECK(drv_allows("+OPL", T_PFS));
  CHECK(drv_allows("PP.HDL.SLUS_203.12", T_HDL));
}

/* ---- installer whitelist (stricter: game pairs + own partitions) ---- */

TEST(installer_policy_game_pairs) {
  CHECK(partition_remove_allowed("__.SLUS-20312..GRAN_TURISMO_4", T_HDL));
  CHECK(partition_remove_allowed("PP.SLUS-20312..GRAN_TURISMO_4", T_PFS));
  CHECK(partition_remove_allowed("__.SLUS-20312..", T_HDL)); /* empty title (hdl-dump allows) */
  /* Wrong type for the role: refused. */
  CHECK(!partition_remove_allowed("__.SLUS-20312..GRAN_TURISMO_4", T_PFS));
  CHECK(!partition_remove_allowed("PP.SLUS-20312..GRAN_TURISMO_4", T_HDL));
}

TEST(installer_policy_own_partitions) {
  CHECK(partition_remove_allowed("PP.UDPFS-INSTALLER", T_PFS));
  CHECK(partition_remove_allowed("PP.UDPFS-TEST", T_PFS));
  CHECK(!partition_remove_allowed("PP.UDPFS-TEST", T_HDL));
}

TEST(installer_policy_refuses_everything_else) {
  CHECK(!partition_remove_allowed("__common", T_PFS));
  CHECK(!partition_remove_allowed("__mbr", 0x0001));
  CHECK(!partition_remove_allowed("__sysconf", T_PFS));
  CHECK(!partition_remove_allowed("__.linux.1", T_EXT2));
  CHECK(!partition_remove_allowed("__.ABC", T_HDL));          /* not a game shape */
  CHECK(!partition_remove_allowed("__.slus-20312..x", T_HDL)); /* lowercase */
  CHECK(!partition_remove_allowed("__xSLUS-20312..X", T_HDL));
  CHECK(!partition_remove_allowed("+OPL", T_PFS));
  CHECK(!partition_remove_allowed("PP.HDL.SLUS_203.12", T_HDL));
  CHECK(!partition_remove_allowed("PP.SLUS-20312..A,B", T_PFS));
  CHECK(!partition_remove_allowed("__.SLUS-20312..A,B", T_HDL));
  CHECK(!partition_remove_allowed("PP.SLUS-20312..AAAAAAAAAAAAAAAAAA", T_PFS)); /* > 32 */
  CHECK(!partition_remove_allowed("", T_PFS));
  CHECK(!partition_remove_allowed(NULL, T_PFS));
}

TEST(sub_partition_dirents_are_not_main) {
  CHECK(hdd_dirent_is_main(T_HDL, 0x0000));
  CHECK(!hdd_dirent_is_main(T_HDL, 0x0001));
  CHECK(!hdd_dirent_is_main(0x0000, 0x0000));
  CHECK(hdd_dirent_is_main(T_PFS, 0x0000));
}

/* Review finding 9: hdl-dump's default (non -hide) installs are visible
 * "PP." partitions of type HDL -- games, not XMB channels. */
TEST(visible_hdl_games_are_not_channels) {
  CHECK(partition_is_xmb_channel("PP.SLUS-20312..GT4", T_PFS));
  CHECK(!partition_is_xmb_channel("PP.SLUS-20312..GT4", T_HDL));
  CHECK(!partition_is_xmb_channel("__.SLUS-20312..GT4", T_PFS));
}

TEST(comma_names_are_not_games) {
  CHECK(!partition_is_hidden_game("__.SLUS-20312..A,B"));
  CHECK(!partition_is_game_channel("PP.SLUS-20312..A,B"));
  CHECK(!partition_is_game_channel("PP.SLUS-20312..a"));
  CHECK(partition_is_game_channel("PP.SLUS-20312..A_1"));
}

TEST(installer_policy_both_installer_names) {
  CHECK(partition_remove_allowed(INSTALLER_PARTITION_NAME, T_PFS));
  CHECK(partition_remove_allowed(INSTALLER_LEGACY_NAME, T_PFS));
  CHECK(!partition_remove_allowed(INSTALLER_PARTITION_NAME, T_HDL));
}
