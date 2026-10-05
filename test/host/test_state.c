#include "../../src/opl_dependency.h"
#include "../../src/settings.h"
#include "../../src/transaction.h"
#include "test.h"

/* ---- transaction ---- */

static const tx_state_t HAPPY[] = {
    TX_NONE,         TX_PLANNED,         TX_HDL_CREATED,
    TX_STREAMING,    TX_HDL_COMPLETE,    TX_HDL_VERIFIED,
    TX_CHANNEL_CREATED, TX_CHANNEL_VERIFIED, TX_COMPLETE};

static void verified_journal(tx_journal_t *j) {
  memset(j, 0, sizeof(*j));
  strcpy(j->startup_id, "SLUS_203.12");
  strcpy(j->visible_partition, "PP.SLUS-20312..GRAN_TURISMO_4");
  strcpy(j->hidden_partition, "__.SLUS-20312..GRAN_TURISMO_4");
  j->bytes_expected = j->bytes_written = j->bytes_verified = 8547991552ull;
  j->has_source_crc = j->has_installed_crc = 1;
  j->source_crc32 = j->installed_crc32 = 0xDEADBEEFu;
  j->state = TX_HDL_VERIFIED;
}

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
  CHECK(!tx_transition_allowed(TX_PLANNED, TX_HDL_VERIFIED));
  CHECK(!tx_transition_allowed(TX_HDL_CREATED, TX_HDL_VERIFIED));
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

TEST(tx_restart_and_channel_repair) {
  CHECK(tx_transition_allowed(TX_FAILED, TX_PLANNED));
  CHECK(tx_transition_allowed(TX_COMPLETE, TX_PLANNED));
  /* Channel (re)build from any state whose data already verified. */
  CHECK(tx_transition_allowed(TX_HDL_VERIFIED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_CHANNEL_CREATED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_CHANNEL_VERIFIED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_FAILED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_COMPLETE, TX_HDL_VERIFIED));
  /* ...but never from nothing: a channel needs a verified journal. */
  CHECK(!tx_transition_allowed(TX_NONE, TX_HDL_VERIFIED));
}

TEST(tx_verified_predicate) {
  tx_journal_t j;
  verified_journal(&j);
  CHECK(tx_hidden_data_verified(&j));
  j.state = TX_COMPLETE;
  CHECK(tx_hidden_data_verified(&j));
  j.state = TX_CHANNEL_CREATED;
  CHECK(tx_hidden_data_verified(&j));

  verified_journal(&j);
  j.state = TX_HDL_COMPLETE; /* copied, not yet read back */
  CHECK(!tx_hidden_data_verified(&j));
  verified_journal(&j);
  j.installed_crc32 ^= 1;
  CHECK(!tx_hidden_data_verified(&j));
  verified_journal(&j);
  j.has_installed_crc = 0;
  CHECK(!tx_hidden_data_verified(&j));
  verified_journal(&j);
  j.bytes_verified -= 2048;
  CHECK(!tx_hidden_data_verified(&j));
  verified_journal(&j);
  j.bytes_written -= 2048;
  CHECK(!tx_hidden_data_verified(&j));
  verified_journal(&j);
  j.deleting = 1; /* a delete was started: data no longer trusted */
  CHECK(!tx_hidden_data_verified(&j));

  /* Failure after verification (channel stage) keeps the data verified;
   * failure before it does not. */
  verified_journal(&j);
  j.state = TX_HDL_VERIFIED;
  tx_fail(&j, ERR_XMB_VERIFY);
  CHECK(tx_hidden_data_verified(&j));
  verified_journal(&j);
  j.state = TX_STREAMING;
  tx_fail(&j, ERR_SOURCE_READ);
  CHECK(!tx_hidden_data_verified(&j));
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

TEST(tx_serialize_roundtrip_large_sizes_and_crc) {
  tx_journal_t j, k;
  verified_journal(&j);
  strcpy(j.source_path, "udpfs:/DVD/Game B.zso.iso");
  j.source_size = 8547991552ull;
  j.bytes_written = 4294967296ull + 2048;
  j.source_crc32 = 0x0000ABCDu;
  j.state = TX_FAILED;
  j.failed_from = TX_STREAMING;
  j.deleting = 1;
  strcpy(j.last_error, "ERR_SOURCE_READ");
  char buf[1024];
  size_t n = tx_serialize(&j, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(strstr(buf, "source_path=udpfs:/DVD/Game B.zso.iso\n") != NULL);
  CHECK(strstr(buf, "bytes_written=4294969344\n") != NULL);
  CHECK(strstr(buf, "source_crc32=0000abcd\n") != NULL);
  CHECK(strstr(buf, "state=TX_FAILED\n") != NULL);
  CHECK(strstr(buf, "deleting=1\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(memcmp(&j, &k, sizeof(j)) == 0);

  /* Unset CRCs serialize empty and parse back as unset. */
  j.has_installed_crc = 0;
  j.installed_crc32 = 0;
  tx_serialize(&j, buf, sizeof(buf));
  CHECK(strstr(buf, "installed_crc32=\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK_EQ_INT(k.has_installed_crc, 0);
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
  CHECK_EQ_INT(tx_parse("startup_id=SLUS_203.12\nvisible_partition=PP.SLUS-20312..A\n"
                        "hidden_partition=__.SLUS-20312..A\nstate=TX_PLANNED\n"
                        "source_crc32=xyz\n", &k), -1);
  CHECK_EQ_INT(tx_parse("startup_id=SLUS_203.12\r\nvisible_partition=PP.SLUS-20312..A\r\n"
                        "hidden_partition=__.SLUS-20312..A\r\nstate=TX_PLANNED\r\n", &k), 0);
}

/* Review finding 1: a journal that failed (last_error set) and is then
 * advanced must serialize/parse back to exactly the in-memory struct,
 * or tx_save's read-back check rejects it forever. */
TEST(tx_advance_after_fail_roundtrips_exactly) {
  tx_journal_t j, k;
  verified_journal(&j);
  tx_fail(&j, ERR_PFS_CREATE);
  CHECK_EQ_INT(tx_advance(&j, TX_HDL_VERIFIED), ERR_OK);
  char buf[1024];
  CHECK(tx_serialize(&j, buf, sizeof(buf)) > 0);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(tx_journal_equal(&j, &k));
  CHECK(memcmp(&j, &k, sizeof(j)) == 0);
}

/* Review finding 10: values keep trailing spaces (only CR/LF stripped). */
TEST(tx_source_path_trailing_space_roundtrips) {
  tx_journal_t j, k;
  verified_journal(&j);
  strcpy(j.source_path, "udpfs:/DVD/Game .iso ");
  char buf[1024];
  tx_serialize(&j, buf, sizeof(buf));
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK_STR(k.source_path, "udpfs:/DVD/Game .iso ");
  CHECK(tx_journal_equal(&j, &k));
}

/* Review finding 2: the journal is bound to the physical partition. */
TEST(tx_partition_identity_roundtrip_and_match) {
  tx_journal_t j, k;
  verified_journal(&j);
  j.hdl_start = 0x12345678u;
  j.hdl_size = 0x00800000u;
  j.hdl_header_crc32 = 0xCAFEF00Du;
  j.has_hdl_identity = 1;
  char buf[1024];
  tx_serialize(&j, buf, sizeof(buf));
  CHECK(strstr(buf, "hdl_start=305419896\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(tx_journal_equal(&j, &k));
  CHECK(tx_identity_matches(&k, 0x12345678u, 0x00800000u, 0xCAFEF00Du));
  CHECK(!tx_identity_matches(&k, 0x12345679u, 0x00800000u, 0xCAFEF00Du));
  CHECK(!tx_identity_matches(&k, 0x12345678u, 0x00800001u, 0xCAFEF00Du));
  CHECK(!tx_identity_matches(&k, 0x12345678u, 0x00800000u, 0xCAFEF00Eu));
  k.has_hdl_identity = 0;
  CHECK(!tx_identity_matches(&k, 0x12345678u, 0x00800000u, 0xCAFEF00Du));
}

TEST(tx_journal_keyed_by_pair) {
  char a[96], b[96];
  CHECK_EQ_INT(tx_journal_filename_for("__.SLUS-20312..GRAN_TURISMO_4", a), 0);
  CHECK_STR(a, "install-SLUS-20312..GRAN_TURISMO_4.ini");
  CHECK_EQ_INT(tx_journal_filename_for("PP.SLUS-20312..GRAN_TURISMO_4", b), 0);
  CHECK_STR(a, b); /* same file from either member */
  CHECK_EQ_INT(tx_journal_filename_for("__.SLUS-20312..GT4", b), 0);
  CHECK(strcmp(a, b) != 0); /* different title, different journal */
  CHECK_EQ_INT(tx_journal_filename_for("__common", b), -1);
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

TEST(tx_verify_skipped_trusts_only_a_complete_copy) {
  tx_journal_t j;
  verified_journal(&j);
  j.verify_skipped = 1; /* read-back skipped by the user */
  j.has_installed_crc = 0;
  j.installed_crc32 = 0;
  j.bytes_verified = 0;
  CHECK(tx_hidden_data_verified(&j));
  CHECK(tx_hidden_data_read_back(&j) == 0);
  j.bytes_written -= 2048; /* copy incomplete: never trusted */
  CHECK(!tx_hidden_data_verified(&j));
  j.bytes_written += 2048;
  j.has_source_crc = 0;
  CHECK(!tx_hidden_data_verified(&j));
  j.has_source_crc = 1;
  j.state = TX_HDL_COMPLETE; /* not yet at the verify decision */
  CHECK(!tx_hidden_data_verified(&j));
  j.state = TX_COMPLETE;
  j.deleting = 1;
  CHECK(!tx_hidden_data_verified(&j));

  verified_journal(&j);
  CHECK(tx_hidden_data_read_back(&j));
}

TEST(tx_verify_skipped_roundtrip_and_old_journals) {
  tx_journal_t j, k;
  verified_journal(&j);
  j.verify_skipped = 1;
  char buf[1200];
  CHECK(tx_serialize(&j, buf, sizeof(buf)) > 0);
  CHECK(strstr(buf, "verify_skipped=1\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(tx_journal_equal(&j, &k));
  k.verify_skipped = 0;
  CHECK(!tx_journal_equal(&j, &k));
  /* A journal written before the field existed reads as verified. */
  verified_journal(&j);
  tx_serialize(&j, buf, sizeof(buf));
  char *p = strstr(buf, "verify_skipped=0\n");
  CHECK(p != NULL);
  if (p)
    memmove(p, p + 17, strlen(p + 17) + 1);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK_EQ_INT(k.verify_skipped, 0);
  CHECK(tx_journal_equal(&j, &k));
}

static void streaming_journal(tx_journal_t *j) {
  verified_journal(j);
  j->state = TX_STREAMING;
  j->bytes_written = 512ull * 1024 * 1024;
  j->bytes_verified = 0;
  j->has_source_crc = j->has_installed_crc = 0;
  j->source_crc32 = j->installed_crc32 = 0; /* not yet known */
  j->has_resume_crc = 1;
  j->resume_crc32 = 0x1234ABCDu;
  j->has_hdl_identity = 1;
  j->hdl_start = 0x40000;
  j->hdl_size = 0x800000;
  j->hdl_header_crc32 = 0x55AA55AAu;
}

TEST(tx_resumable_rules) {
  tx_journal_t j;
  streaming_journal(&j);
  CHECK(tx_resumable(&j)); /* power cut mid-copy */
  tx_fail(&j, ERR_SOURCE_READ);
  CHECK(tx_resumable(&j)); /* network failure mid-copy */
  CHECK(tx_transition_allowed(j.state, TX_STREAMING));
  streaming_journal(&j);
  CHECK(tx_transition_allowed(TX_STREAMING, TX_STREAMING));
  j.has_resume_crc = 0; /* no checkpoint yet: nothing to resume from */
  CHECK(!tx_resumable(&j));
  streaming_journal(&j);
  j.bytes_written = 0;
  CHECK(!tx_resumable(&j));
  streaming_journal(&j);
  j.bytes_written = j.bytes_expected; /* copy finished: verify, not resume */
  CHECK(!tx_resumable(&j));
  streaming_journal(&j);
  j.bytes_written += 1; /* not on a sector boundary */
  CHECK(!tx_resumable(&j));
  streaming_journal(&j);
  j.deleting = 1;
  CHECK(!tx_resumable(&j));
  streaming_journal(&j);
  j.has_hdl_identity = 0;
  CHECK(!tx_resumable(&j));
  streaming_journal(&j);
  j.state = TX_HDL_CREATED;
  tx_fail(&j, ERR_HDL_WRITE); /* failed before any data */
  CHECK(!tx_resumable(&j));
  CHECK(!tx_transition_allowed(TX_HDL_COMPLETE, TX_STREAMING));
  CHECK(!tx_transition_allowed(TX_COMPLETE, TX_STREAMING));
}

TEST(tx_resume_crc_roundtrip) {
  tx_journal_t j, k;
  streaming_journal(&j);
  char buf[1200];
  CHECK(tx_serialize(&j, buf, sizeof(buf)) > 0);
  CHECK(strstr(buf, "resume_crc32=1234abcd\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(tx_journal_equal(&j, &k));
  CHECK_EQ_INT(k.has_resume_crc, 1);
  j.has_resume_crc = 0;
  tx_serialize(&j, buf, sizeof(buf));
  CHECK(strstr(buf, "resume_crc32=\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK_EQ_INT(k.has_resume_crc, 0);
}

TEST(tx_launcher_and_opl_cfg_fields_roundtrip) {
  tx_journal_t j, k;
  verified_journal(&j);
  strcpy(j.launcher_source, "server");
  strcpy(j.opl_cfg, "copied");
  char buf[1200];
  CHECK(tx_serialize(&j, buf, sizeof(buf)) > 0);
  CHECK(strstr(buf, "launcher_source=server\n") != NULL);
  CHECK(strstr(buf, "opl_cfg=copied\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(tx_journal_equal(&j, &k));
  strcpy(k.opl_cfg, "kept");
  CHECK(!tx_journal_equal(&j, &k));
}
