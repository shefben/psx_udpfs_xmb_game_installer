#include <string.h>

#include "../../src/apps.h"
#include "../../src/partname.h"
#include "test.h"

TEST(apps_partition_classification) {
  /* PSX-XMB-Manager / PFS-BatchKit-Manager / this installer's apps */
  CHECK_EQ_INT(partition_is_app("PP.APPS-00001..WLE_LATEST"), 1);
  CHECK_EQ_INT(partition_is_app("PP.UAPP-00003..MYAPP"), 1);
  CHECK_EQ_INT(partition_is_app("__.APPS-00001..WLE"), 0);
  CHECK_EQ_INT(partition_is_app("PP.SLUS-20312..GAME"), 0);
  CHECK_EQ_INT(partition_is_app("PP.APPS-0001..X"), 0);
  CHECK_EQ_INT(partition_is_app(NULL), 0);
  /* apps are no games */
  CHECK_EQ_INT(partition_is_game_channel("PP.APPS-00001..WLE"), 0);
  CHECK_EQ_INT(partition_is_hidden_game("__.APPS-00001..WLE"), 0);
  CHECK_EQ_INT(partition_is_game_channel("PP.SLUS-20312..GAME"), 1);
  /* removable only as PFS */
  CHECK_EQ_INT(partition_remove_allowed("PP.APPS-00001..WLE", APA_TYPE_PFS_ID), 1);
  CHECK_EQ_INT(partition_remove_allowed("PP.APPS-00001..WLE", APA_TYPE_HDL_ID), 0);
  CHECK_EQ_INT(partition_remove_allowed("PP.APPS-00001..W,X", APA_TYPE_PFS_ID), 0);
}

TEST(apps_partition_name_next_free_number) {
  const char *names[] = {"__mbr", "PP.APPS-00001..WLE_LATEST", "PP.APPS-00003..OPL",
                         "PP.SLUS-20312..GAME", "PP.UAPP-00002..X"};
  char out[APA_NAME_MAX + 1];
  CHECK_EQ_INT(app_partition_name("wLaunchELF 4.43a", names, 5, out), 0);
  CHECK_STR(out, "PP.APPS-00002..WLAUNCHELF_4_43A");
  CHECK_EQ_INT(partition_is_app(out), 1);
  CHECK_EQ_INT(app_partition_name("x", NULL, 0, out), 0);
  CHECK_STR(out, "PP.APPS-00001..X");
  /* long titles are cut to the 32-byte APA name */
  CHECK_EQ_INT(app_partition_name("A very long homebrew application name", NULL, 0, out), 0);
  CHECK_EQ_INT((int)strlen(out), APA_NAME_MAX);
  CHECK_STR(out, "PP.APPS-00001..A_VERY_LONG_HOMEB");
  /* nothing usable in the title */
  CHECK_EQ_INT(app_partition_name("!!!", NULL, 0, out), 0);
  CHECK_STR(out, "PP.APPS-00001..APP");
}

TEST(apps_cfg_roundtrip) {
  app_cfg_t c;
  memset(&c, 0, sizeof(c));
  strcpy(c.boot, "OPL/OPNPS2LD.ELF");
  strcpy(c.args[0], "-foo");
  strcpy(c.args[1], "bar baz");
  c.nargs = 2;
  char text[512];
  size_t n = app_cfg_render(&c, text, sizeof(text));
  CHECK(n > 0);
  CHECK(strstr(text, "boot = OPL/OPNPS2LD.ELF\n") != NULL);
  app_cfg_t d;
  CHECK_EQ_INT(app_cfg_parse(text, n, &d), 0);
  CHECK_STR(d.boot, "OPL/OPNPS2LD.ELF");
  CHECK_EQ_INT(d.nargs, 2);
  CHECK_STR(d.args[1], "bar baz");
  CHECK_EQ_INT((int)app_cfg_render(&c, text, 8), 0);
}

TEST(apps_cfg_parse_rules) {
  app_cfg_t c;
  const char *t = "# comment\r\n  boot =  BOOT.ELF  \r\narg=one\r\n\r\nunknown = x\r\n";
  CHECK_EQ_INT(app_cfg_parse(t, strlen(t), &c), 0);
  CHECK_STR(c.boot, "BOOT.ELF");
  CHECK_EQ_INT(c.nargs, 1);
  CHECK_STR(c.args[0], "one");
  /* no boot line, or one leaving the partition */
  CHECK_EQ_INT(app_cfg_parse("arg = x\n", 8, &c), -1);
  const char *bad[] = {"boot = mc0:/X.ELF\n", "boot = ../X.ELF\n", "boot = /X.ELF\n",
                       "boot = A\\B.ELF\n", "boot = \n"};
  for (int i = 0; i < 5; i++)
    CHECK_EQ_INT(app_cfg_parse(bad[i], strlen(bad[i]), &c), -1);
  /* more args than fit are dropped, not overflowed */
  char many[1024] = "boot = B.ELF\n";
  for (int i = 0; i < APP_ARGS_MAX + 3; i++)
    strcat(many, "arg = x\n");
  CHECK_EQ_INT(app_cfg_parse(many, strlen(many), &c), 0);
  CHECK_EQ_INT(c.nargs, APP_ARGS_MAX);
  /* not NUL-terminated input is bounded by len */
  char raw[16];
  memcpy(raw, "boot = B.ELFxxxx", 16);
  CHECK_EQ_INT(app_cfg_parse(raw, 12, &c), 0);
  CHECK_STR(c.boot, "B.ELF");
}

TEST(apps_path_ok) {
  CHECK_EQ_INT(app_path_ok("BOOT.ELF"), 1);
  CHECK_EQ_INT(app_path_ok("OPL/OPNPS2LD.ELF"), 1);
  CHECK_EQ_INT(app_path_ok(""), 0);
  CHECK_EQ_INT(app_path_ok("a/../b"), 0);
  CHECK_EQ_INT(app_path_ok("pfs0:x"), 0);
  CHECK_EQ_INT(app_path_ok("/x"), 0);
  CHECK_EQ_INT(app_path_ok("APP.CFG"), 0); /* reserved */
  CHECK_EQ_INT(app_path_ok("EXECUTE.KELF"), 0);
  CHECK_EQ_INT(app_path_ok("res/info.sys"), 0);
}

TEST(apps_size) {
  uint32_t mb = 0;
  CHECK_STR(app_size_str(0, &mb), "128M");
  CHECK_EQ_INT((int)mb, 128);
  CHECK_STR(app_size_str(100, &mb), "128M");
  CHECK_STR(app_size_str(120, &mb), "256M"); /* 16 MiB kept free for PFS and res/ */
  CHECK_STR(app_size_str(1000, &mb), "1G");
  CHECK_EQ_INT((int)mb, 1024);
  CHECK_STR(app_size_str(2032, &mb), "2G");
  CHECK(app_size_str(2033, &mb) == NULL);
}

TEST(apps_title_from_name) {
  char t[64];
  app_title_from_name("wLaunchELF_4.43a.elf", t, sizeof(t));
  CHECK_STR(t, "wLaunchELF 4.43a");
  app_title_from_name("OPNPS2LD.ELF", t, sizeof(t));
  CHECK_STR(t, "OPNPS2LD");
  app_title_from_name("My App", t, sizeof(t));
  CHECK_STR(t, "My App");
  app_title_from_name(".elf", t, sizeof(t));
  CHECK_STR(t, "App");
}
