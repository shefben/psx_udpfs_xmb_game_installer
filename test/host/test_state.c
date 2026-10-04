#include "../../src/opl_dependency.h"
#include "../../src/settings.h"
#include "../../src/transaction.h"
#include "test.h"

/* ---- transaction ---- */

static const tx_state_t HAPPY[] = {
    TX_NONE,         TX_PLANNED,         TX_HDL_CREATED,
    TX_STREAMING,    TX_HDL_COMPLETE,    TX_HDL_VERIFIED,
    TX_CHANNEL_CREATED, TX_CHANNEL_VERIFIED, TX_COMPLETE};

TEST(tx_happy_path) {
  tx_journal_t j;
  memset(&j, 0, sizeof(j));
  for (unsigned i = 1; i < sizeof(HAPPY) / sizeof(HAPPY[0]); i++)
    CHECK_EQ_INT(tx_advance(&j, HAPPY[i]), ERR_OK);
  CHECK_EQ_INT(j.state, TX_COMPLETE);
}

TEST(tx_no_skipping_forward) {
  CHECK(!tx_transition_allowed(TX_PLANNED, TX_STREAMING));
  CHECK(!tx_transition_allowed(TX_STREAMING, TX_HDL_VERIFIED));
  CHECK(!tx_transition_allowed(TX_HDL_COMPLETE, TX_CHANNEL_CREATED));
  CHECK(!tx_transition_allowed(TX_CHANNEL_CREATED, TX_COMPLETE));
  CHECK(!tx_transition_allowed(TX_HDL_VERIFIED, TX_COMPLETE));
  CHECK(!tx_transition_allowed(TX_STREAMING, TX_HDL_CREATED));
}

TEST(tx_channel_only_after_hdl_verified) {
  tx_journal_t j;
  memset(&j, 0, sizeof(j));
  for (int s = TX_NONE; s < TX__COUNT; s++) {
    j.state = (tx_state_t)s;
    int expect = s == TX_HDL_VERIFIED || s == TX_CHANNEL_CREATED ||
                 s == TX_CHANNEL_VERIFIED;
    CHECK_EQ_INT(tx_channel_creation_allowed(&j), expect);
  }
}

TEST(tx_fail_from_any_unfinished) {
  for (int s = TX_PLANNED; s < TX_COMPLETE; s++)
    CHECK(tx_transition_allowed((tx_state_t)s, TX_FAILED));
  CHECK(!tx_transition_allowed(TX_COMPLETE, TX_FAILED));
  CHECK(!tx_transition_allowed(TX_NONE, TX_FAILED));
  tx_journal_t j;
  memset(&j, 0, sizeof(j));
  j.state = TX_STREAMING;
  tx_fail(&j, ERR_HDL_WRITE);
  CHECK_EQ_INT(j.state, TX_FAILED);
  CHECK_EQ_INT(j.failed_from, TX_STREAMING);
  CHECK_STR(j.last_error, "ERR_HDL_WRITE");
  CHECK(!tx_channel_creation_allowed(&j));
}

TEST(tx_restart_and_repair) {
  CHECK(tx_transition_allowed(TX_FAILED, TX_PLANNED));
  CHECK(tx_transition_allowed(TX_COMPLETE, TX_PLANNED));
  CHECK(tx_transition_allowed(TX_NONE, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_FAILED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_COMPLETE, TX_HDL_VERIFIED));
  CHECK(!tx_transition_allowed(TX_PLANNED, TX_HDL_VERIFIED));
}

TEST(tx_state_names_roundtrip) {
  for (int s = 0; s < TX__COUNT; s++) {
    tx_state_t p;
    CHECK_EQ_INT(tx_state_parse(tx_state_name((tx_state_t)s), &p), 0);
    CHECK_EQ_INT(p, s);
  }
  tx_state_t p;
  CHECK_EQ_INT(tx_state_parse("TX_BOGUS", &p), -1);
}

TEST(tx_serialize_roundtrip_large_sizes) {
  tx_journal_t j, k;
  memset(&j, 0, sizeof(j));
  strcpy(j.source_path, "udpfs:/DVD/Game B.zso.iso");
  j.source_size = 8547991552ull;
  strcpy(j.startup_id, "SLUS_203.12");
  strcpy(j.visible_partition, "PP.SLUS-20312..GRAN_TURISMO_4");
  strcpy(j.hidden_partition, "__.SLUS-20312..GRAN_TURISMO_4");
  j.bytes_expected = 8547991552ull;
  j.bytes_written = 4294967296ull + 2048;
  j.state = TX_FAILED;
  j.failed_from = TX_STREAMING;
  strcpy(j.last_error, "ERR_SOURCE_READ");
  char buf[1024];
  size_t n = tx_serialize(&j, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(strstr(buf, "source_path=udpfs:/DVD/Game B.zso.iso\n") != NULL);
  CHECK(strstr(buf, "bytes_written=4294969344\n") != NULL);
  CHECK(strstr(buf, "state=TX_FAILED\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(memcmp(&j, &k, sizeof(j)) == 0);
}

TEST(tx_parse_rejects_garbage) {
  tx_journal_t k;
  CHECK_EQ_INT(tx_parse("", &k), -1);
  CHECK_EQ_INT(tx_parse("state=TX_PLANNED\n", &k), -1); /* missing names */
  CHECK_EQ_INT(tx_parse("startup_id=SLUS_203.12\nvisible_partition=PP.SLUS-20312..A\n"
                        "hidden_partition=__.SLUS-20312..A\nstate=TX_NOPE\n", &k), -1);
  CHECK_EQ_INT(tx_parse("startup_id=SLUS_203.12\nvisible_partition=PP.SLUS-20312..A\n"
                        "hidden_partition=__.SLUS-20312..B\nstate=TX_PLANNED\n", &k), -1);
  CHECK_EQ_INT(tx_parse("startup_id=SLUS_203.12\nvisible_partition=PP.SLUS-20312..A\n"
                        "hidden_partition=__.SLUS-20312..A\nstate=TX_PLANNED\n"
                        "bytes_written=12x\n", &k), -1);
  CHECK_EQ_INT(tx_parse("startup_id=SLUS_203.12\r\nvisible_partition=PP.SLUS-20312..A\r\n"
                        "hidden_partition=__.SLUS-20312..A\r\nstate=TX_PLANNED\r\n", &k), 0);
}

TEST(tx_filename) {
  char f[64];
  CHECK_EQ_INT(tx_journal_filename("SLUS_203.12", f), 0);
  CHECK_STR(f, "install-SLUS-20312.ini");
  CHECK_EQ_INT(tx_journal_filename("bad", f), -1);
}

/* ---- settings ---- */

TEST(ip_validation) {
  CHECK(ip_is_valid("192.168.1.10"));
  CHECK(ip_is_valid("10.0.0.1"));
  CHECK(ip_is_valid("172.16.255.254"));
  CHECK(!ip_is_valid("192.168.1"));
  CHECK(!ip_is_valid("192.168.1.256"));
  CHECK(!ip_is_valid("192.168.1.1.1"));
  CHECK(!ip_is_valid("192.168..1"));
  CHECK(!ip_is_valid("192.168.1.-1"));
  CHECK(!ip_is_valid(" 192.168.1.1"));
  CHECK(!ip_is_valid("192.168.1.1 "));
  CHECK(!ip_is_valid("192.168.1.0001"));
  CHECK(!ip_is_valid("0.0.0.0"));
  CHECK(!ip_is_valid("127.0.0.1"));
  CHECK(!ip_is_valid("224.0.0.1"));
  CHECK(!ip_is_valid("255.255.255.255"));
  CHECK(!ip_is_valid(""));
  CHECK(!ip_is_valid("a.b.c.d"));
}

TEST(settings_parse_valid_and_fallbacks) {
  net_settings_t s;
  settings_parse("local_ip=10.0.0.7\n", &s);
  CHECK_STR(s.local_ip, "10.0.0.7");
  CHECK_EQ_INT(s.using_default, 0);
  CHECK_EQ_INT(s.warning, 0);

  settings_parse("# comment\r\nlocal_ip = 10.0.0.8\r\n", &s);
  CHECK_STR(s.local_ip, "10.0.0.8");

  settings_parse(NULL, &s);
  CHECK_STR(s.local_ip, SETTINGS_DEFAULT_IP);
  CHECK_EQ_INT(s.using_default, 1);
  CHECK_EQ_INT(s.warning, 0);

  settings_parse("local_ip=300.1.1.1\n", &s);
  CHECK_STR(s.local_ip, SETTINGS_DEFAULT_IP);
  CHECK_EQ_INT(s.using_default, 1);
  CHECK_EQ_INT(s.warning, 1);

  settings_parse("other=1\n", &s);
  CHECK_EQ_INT(s.warning, 1);
}

TEST(settings_serialize_roundtrip) {
  net_settings_t s, t;
  settings_parse("local_ip=192.168.0.50\n", &s);
  char buf[128];
  CHECK(settings_serialize(&s, buf, sizeof(buf)) > 0);
  CHECK_STR(buf, "local_ip=192.168.0.50\n");
  settings_parse(buf, &t);
  CHECK_STR(t.local_ip, "192.168.0.50");
}

TEST(ip_adjust_octet_wraps) {
  char ip[16] = "192.168.1.255";
  CHECK_EQ_INT(ip_adjust_octet(ip, 3, 1), 0);
  CHECK_STR(ip, "192.168.1.0");
  CHECK_EQ_INT(ip_adjust_octet(ip, 0, -1), 0);
  CHECK_STR(ip, "191.168.1.0");
  CHECK_EQ_INT(ip_adjust_octet(ip, 2, -2), 0);
  CHECK_STR(ip, "191.168.255.0");
  CHECK_EQ_INT(ip_adjust_octet(ip, 4, 1), -1);
}

/* ---- OPL runtime resolution ---- */

TEST(opl_resolve_defaults) {
  opl_runtime_t r;
  CHECK_EQ_INT(opl_resolve_from_conf(NULL, &r), 0);
  CHECK_STR(r.partition, "+OPL");
  CHECK_STR(r.elf_path, "OPNPS2LD.ELF");
  CHECK_EQ_INT(r.from_config, 0);
  CHECK_EQ_INT(opl_resolve_from_conf("", &r), 0);
  CHECK_STR(r.partition, "+OPL");
}

TEST(opl_resolve_from_config) {
  opl_runtime_t r;
  CHECK_EQ_INT(opl_resolve_from_conf("hdd_partition=+OPL\r\nother=1\r\n", &r), 0);
  CHECK_STR(r.partition, "+OPL");
  CHECK_STR(r.elf_path, "OPNPS2LD.ELF");
  CHECK_EQ_INT(r.from_config, 1);
  CHECK_EQ_INT(opl_resolve_from_conf("hdd_partition=__common\n", &r), 0);
  CHECK_STR(r.partition, "__common");
  CHECK_STR(r.elf_path, "OPL/OPNPS2LD.ELF");
  /* Only the first line is consulted, like the launcher's fgets. */
  CHECK_EQ_INT(opl_resolve_from_conf("x=PP.OPL\nhdd_partition=+OPL\n", &r), 0);
  CHECK_STR(r.partition, "PP.OPL");
}

TEST(opl_resolve_malformed) {
  opl_runtime_t r;
  CHECK_EQ_INT(opl_resolve_from_conf("no equals sign\n", &r), -1);
  CHECK_EQ_INT(r.config_malformed, 1);
  CHECK_EQ_INT(opl_resolve_from_conf("hdd_partition=%s\n", &r), -1);
  CHECK_EQ_INT(opl_resolve_from_conf("hdd_partition=\n", &r), -1);
  CHECK_EQ_INT(opl_resolve_from_conf(
                   "hdd_partition=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n", &r),
               -1);
}
