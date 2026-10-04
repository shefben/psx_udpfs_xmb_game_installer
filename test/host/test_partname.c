#include "../../src/partname.h"
#include "test.h"

static void pair(const char *id, const char *title, const char *exp_vis) {
  char v[33], h[33];
  CHECK_EQ_INT(build_game_partition_pair(id, title, v, h), 0);
  CHECK_STR(v, exp_vis);
  CHECK(h[0] == '_' && h[1] == '_');
  CHECK_STR(h + 2, v + 2);
  CHECK(partition_pair_matches(v, h));
}

TEST(partname_slus) { pair("SLUS_203.12", "Gran Turismo 4", "PP.SLUS-20312..GRAN_TURISMO_4"); }
TEST(partname_scus) { pair("SCUS_971.99", "ICO", "PP.SCUS-97199..ICO"); }
TEST(partname_sles) { pair("SLES_503.30", "Rez", "PP.SLES-50330..REZ"); }
TEST(partname_sces) { pair("SCES_503.62", "Shadow of the Colossus", "PP.SCES-50362..SHADOW_OF_THE_CO"); }
TEST(partname_slps) { pair("SLPS_250.50", "abc", "PP.SLPS-25050..ABC"); }
TEST(partname_slpm) { pair("SLPM_650.51", "x y", "PP.SLPM-65051..X_Y"); }

TEST(partname_punctuation_and_spaces) {
  pair("SLUS_210.65", "Metal Gear Solid 3: Snake", "PP.SLUS-21065..METAL_GEAR_SOLID");
  pair("SLUS_200.62", "GTA: III", "PP.SLUS-20062..GTA__III");
  pair("SLUS_200.62", "a-b.c'd", "PP.SLUS-20062..A_B_C_D");
}

TEST(partname_long_title_truncates_after_id) {
  char v[33], h[33];
  CHECK_EQ_INT(build_game_partition_pair(
                   "SLUS_203.12", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", v, h),
               0);
  CHECK_EQ_INT(strlen(v), 31);
  CHECK(strlen(v) <= APA_NAME_MAX);
  CHECK_STR(v, "PP.SLUS-20312..AAAAAAAAAAAAAAAA");
}

TEST(partname_non_ascii_bytes) {
  pair("SLPM_650.51", "\xe3\x83\x86st", "PP.SLPM-65051..___ST");
}

TEST(partname_duplicate_sanitized_titles_collide) {
  char v1[33], h1[33], v2[33], h2[33];
  build_game_partition_pair("SLUS_200.62", "GTA: III", v1, h1);
  build_game_partition_pair("SLUS_200.62", "GTA! III", v2, h2);
  CHECK_STR(v1, v2);
  CHECK_STR(h1, h2);
}

TEST(partname_rejects_invalid_id) {
  char v[33], h[33];
  CHECK_EQ_INT(build_game_partition_pair("", "x", v, h), -1);
  CHECK_EQ_INT(build_game_partition_pair("SLUS20312", "x", v, h), -1);
  CHECK_EQ_INT(build_game_partition_pair("SLUS_2O3.12", "x", v, h), -1);
  CHECK_EQ_INT(build_game_partition_pair(NULL, "x", v, h), -1);
}

TEST(boot_id_from_boot2_forms) {
  char out[16];
  CHECK_EQ_INT(boot_id_from_boot2("cdrom0:\\SLUS_203.12;1", out), 0);
  CHECK_STR(out, "SLUS_203.12");
  CHECK_EQ_INT(boot_id_from_boot2("cdrom0:\\SCES_503.62", out), 0);
  CHECK_STR(out, "SCES_503.62");
  CHECK_EQ_INT(boot_id_from_boot2("cdrom0:/SLES_503.30;1", out), 0);
  CHECK_STR(out, "SLES_503.30");
  CHECK_EQ_INT(boot_id_from_boot2("cdrom0:\\DATA\\SLPM_650.51;1", out), 0);
  CHECK_STR(out, "SLPM_650.51");
  CHECK_EQ_INT(boot_id_from_boot2("cdrom0:\\slus_203.12;1", out), 0);
  CHECK_STR(out, "SLUS_203.12");
  CHECK_EQ_INT(boot_id_from_boot2("cdrom0:\\MAIN.ELF;1", out), -1);
  CHECK_EQ_INT(boot_id_from_boot2("", out), -1);
}

TEST(boot_id_to_part_id_forms) {
  char out[11];
  CHECK_EQ_INT(boot_id_to_part_id("SLUS_203.12", out), 0);
  CHECK_STR(out, "SLUS-20312");
  CHECK_EQ_INT(boot_id_to_part_id("SLUS_203.1", out), -1);
}

TEST(part_id_to_boot_id_forms) {
  char out[16];
  CHECK_EQ_INT(part_id_from_partition("__.SLUS-20312..GRAN_TURISMO_4", out), 0);
  CHECK_STR(out, "SLUS_203.12");
  CHECK_EQ_INT(part_id_from_partition("PP.SCES-50362..X", out), 0);
  CHECK_STR(out, "SCES_503.62");
  CHECK_EQ_INT(part_id_from_partition("PP.UDPFS-INSTALLER", out), -1);
  CHECK_EQ_INT(part_id_from_partition("+OPL", out), -1);
}

TEST(partname_classify) {
  CHECK(partition_is_game_channel("PP.SLUS-20312..GRAN_TURISMO_4"));
  CHECK(!partition_is_game_channel("PP.UDPFS-INSTALLER"));
  CHECK(!partition_is_game_channel("__.SLUS-20312..GRAN_TURISMO_4"));
  CHECK(partition_is_hidden_game("__.SLUS-20312..GRAN_TURISMO_4"));
  CHECK(!partition_is_hidden_game("__common"));
  CHECK(!partition_is_hidden_game("__sysconf"));
  CHECK(partition_is_game_channel("PP.SLUS-20312.."));
}

TEST(partname_partner) {
  char out[33];
  CHECK_EQ_INT(partition_partner("PP.SLUS-20312..X", out), 0);
  CHECK_STR(out, "__.SLUS-20312..X");
  CHECK_EQ_INT(partition_partner("__.SLUS-20312..X", out), 0);
  CHECK_STR(out, "PP.SLUS-20312..X");
  CHECK_EQ_INT(partition_partner("+OPL", out), -1);
}

TEST(partname_pair_matches_rejects) {
  CHECK(!partition_pair_matches("PP.SLUS-20312..A", "__.SLUS-20312..B"));
  CHECK(!partition_pair_matches("PP.SLUS-20312..A", "PP.SLUS-20312..A"));
  CHECK(!partition_pair_matches("__.SLUS-20312..A", "PP.SLUS-20312..A"));
}

TEST(region_labels) {
  CHECK_STR(region_label("SLUS_203.12"), "NTSC-U");
  CHECK_STR(region_label("SCUS_971.99"), "NTSC-U");
  CHECK_STR(region_label("SLES_503.30"), "PAL");
  CHECK_STR(region_label("SCES_503.62"), "PAL");
  CHECK_STR(region_label("SLPS_250.50"), "NTSC-J");
  CHECK_STR(region_label("SLPM_650.51"), "NTSC-J");
  CHECK_STR(region_label("SCPS_150.01"), "NTSC-J");
  CHECK_STR(region_label("SCAJ_200.01"), "NTSC-J");
  CHECK_STR(region_label("SCKA_200.01"), "NTSC-J");
  CHECK_STR(region_label("ABCD_123.45"), "UNKNOWN");
  CHECK_STR(region_label(""), "UNKNOWN");
}
