/* Tests for the final-review fix pass. */
#include "../../src/game_pair.h"
#include "../../src/hdl_header.h"
#include "../../src/partname.h"
#include "../../src/transaction.h"
#include "../../src/util.h"
#include "test.h"

/* Finding 6: names containing ',' are parsed as id,password by the APA
 * driver; never treat them as game partitions or remove them. */
TEST(fix_comma_names_are_not_games) {
  CHECK(!partition_is_hidden_game("__.SLUS-20312..A,B"));
  CHECK(!partition_is_game_channel("PP.SLUS-20312..A,B"));
  CHECK(!partition_is_game_channel("PP.SLUS-20312..a"));
  CHECK(partition_is_game_channel("PP.SLUS-20312..A_1"));
}

/* Finding 1: the patched driver allows removing "__" partitions, so the
 * app must refuse system partitions itself. */
TEST(fix_remove_allowed_guard) {
  CHECK(partition_remove_allowed("__.SLUS-20312..GRAN_TURISMO_4"));
  CHECK(partition_remove_allowed("PP.SLUS-20312..GRAN_TURISMO_4"));
  CHECK(partition_remove_allowed("PP.UDPFS-INSTALLER"));
  CHECK(!partition_remove_allowed("__common"));
  CHECK(!partition_remove_allowed("__mbr"));
  CHECK(!partition_remove_allowed("__sysconf"));
  CHECK(!partition_remove_allowed("__.linux.1"));
  CHECK(!partition_remove_allowed("+OPL"));
  CHECK(!partition_remove_allowed("PP.SLUS-20312..A,B"));
  CHECK(!partition_remove_allowed(""));
  CHECK(!partition_remove_allowed("PP.SLUS-20312..AAAAAAAAAAAAAAAAAA"));
  CHECK(!partition_remove_allowed(NULL));
}

/* Finding 3: channel repair from a journal left at HDL_VERIFIED (OPL
 * missing) or mid-channel (power loss) must be allowed. */
TEST(fix_repair_transitions) {
  CHECK(tx_transition_allowed(TX_HDL_VERIFIED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_CHANNEL_CREATED, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_CHANNEL_VERIFIED, TX_HDL_VERIFIED));
  CHECK(!tx_transition_allowed(TX_STREAMING, TX_HDL_VERIFIED));
  CHECK(tx_transition_allowed(TX_HDL_COMPLETE, TX_HDL_VERIFIED)); /* normal step */
  CHECK(!tx_transition_allowed(TX_PLANNED, TX_HDL_VERIFIED));
  CHECK(!tx_transition_allowed(TX_HDL_CREATED, TX_HDL_VERIFIED));
}

/* Finding 2a: journals are keyed by the full pair, not the game ID. */
TEST(fix_journal_keyed_by_partition) {
  char a[96], b[96];
  CHECK_EQ_INT(tx_journal_filename_for("__.SLUS-20312..GRAN_TURISMO_4", a), 0);
  CHECK_STR(a, "install-SLUS-20312..GRAN_TURISMO_4.ini");
  CHECK_EQ_INT(tx_journal_filename_for("__.SLUS-20312..GT4", b), 0);
  CHECK(strcmp(a, b) != 0);
  CHECK_EQ_INT(tx_journal_filename_for("PP.SLUS-20312..GT4", b), 0);
  CHECK_STR(b, "install-SLUS-20312..GT4.ini"); /* same key from either side */
  CHECK_EQ_INT(tx_journal_filename_for("__common", b), -1);
}

/* Finding 2b: completion marker in the HDL header's reserved field. */
TEST(fix_hdl_marker_roundtrip) {
  uint8_t buf[HDL_HEADER_SIZE];
  memset(buf, 0, sizeof(buf));
  put_u32le(buf, HDL_INFO_MAGIC);
  buf[6] = 1;
  strcpy((char *)buf + 8, "T");
  strcpy((char *)buf + 0xAC, "SLUS_203.12");
  put_u32le(buf + 0xF0, 1);
  hdl_header_info_t h;
  CHECK_EQ_INT(hdl_header_parse(buf, &h), 0);
  CHECK_EQ_INT(h.marker, HDL_MARK_NONE);
  hdl_header_set_marker(buf, HDL_MARK_INCOMPLETE);
  CHECK_EQ_INT(hdl_header_parse(buf, &h), 0);
  CHECK_EQ_INT(h.marker, HDL_MARK_INCOMPLETE);
  hdl_header_set_marker(buf, HDL_MARK_COMPLETE);
  CHECK_EQ_INT(hdl_header_parse(buf, &h), 0);
  CHECK_EQ_INT(h.marker, HDL_MARK_COMPLETE);
  /* Only bytes 4..5 change; version and the rest are untouched. */
  CHECK_EQ_INT(buf[6], 1);
  CHECK_EQ_INT(get_u32le(buf), HDL_INFO_MAGIC);
  buf[4] = 0x12;
  buf[5] = 0x34;
  hdl_header_parse(buf, &h);
  CHECK_EQ_INT(h.marker, HDL_MARK_NONE); /* unknown value = no marker */
}

static pair_facts_t pf(int marker, int has_j, tx_state_t js) {
  pair_facts_t f;
  memset(&f, 0, sizeof(f));
  f.hidden_exists = 1;
  f.hidden_header_valid = 1;
  f.hidden_marker = marker;
  f.has_journal = has_j;
  f.journal_state = js;
  return f;
}

TEST(fix_trust_uses_marker) {
  pair_facts_t f;
  /* Journal lost, interrupted copy: marker says incomplete. */
  f = pf(HDL_MARK_INCOMPLETE, 0, TX_NONE);
  CHECK(!pair_hidden_trusted(&f));
  /* Journal lost after a verified copy: marker says complete. */
  f = pf(HDL_MARK_COMPLETE, 0, TX_NONE);
  CHECK(pair_hidden_trusted(&f));
  /* Incomplete marker wins over any journal. */
  f = pf(HDL_MARK_INCOMPLETE, 1, TX_COMPLETE);
  CHECK(!pair_hidden_trusted(&f));
  /* A journal that never reached verify still blocks trust. */
  f = pf(HDL_MARK_NONE, 1, TX_STREAMING);
  CHECK(!pair_hidden_trusted(&f));
  /* Foreign game (other tools): no marker, no journal. */
  f = pf(HDL_MARK_NONE, 0, TX_NONE);
  CHECK(pair_hidden_trusted(&f));
  f = pf(HDL_MARK_COMPLETE, 1, TX_HDL_VERIFIED);
  CHECK(pair_hidden_trusted(&f));
}

/* Finding 5: sub-partition dirents (APA_FLAG_SUB) are not games. */
TEST(fix_sub_partitions_filtered) {
  CHECK(hdd_dirent_is_main(0x1337, 0x0000));
  CHECK(!hdd_dirent_is_main(0x1337, 0x0001));
  CHECK(!hdd_dirent_is_main(0x0000, 0x0000)); /* free space */
  CHECK(hdd_dirent_is_main(0x0100, 0x0000));
}
