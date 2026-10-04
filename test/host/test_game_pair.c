#include "../../src/game_pair.h"
#include "test.h"

static pair_facts_t facts(int he, int hv, int vj, tx_state_t js, tx_state_t ff,
                          int ve, int vv) {
  pair_facts_t f;
  memset(&f, 0, sizeof(f));
  f.hidden_exists = he;
  f.hidden_header_valid = hv;
  f.has_journal = vj;
  f.journal_state = js;
  f.journal_failed_from = ff;
  f.visible_exists = ve;
  f.visible_valid = vv;
  return f;
}

TEST(pair_trust_rules) {
  pair_facts_t f = facts(1, 1, 0, TX_NONE, TX_NONE, 0, 0);
  CHECK(pair_hidden_trusted(&f)); /* foreign game, no journal */
  f = facts(1, 0, 0, TX_NONE, TX_NONE, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
  f = facts(1, 1, 1, TX_STREAMING, TX_NONE, 0, 0);
  CHECK(!pair_hidden_trusted(&f)); /* power loss mid-copy */
  f = facts(1, 1, 1, TX_HDL_COMPLETE, TX_NONE, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
  f = facts(1, 1, 1, TX_HDL_VERIFIED, TX_NONE, 0, 0);
  CHECK(pair_hidden_trusted(&f));
  f = facts(1, 1, 1, TX_COMPLETE, TX_NONE, 0, 0);
  CHECK(pair_hidden_trusted(&f));
  f = facts(1, 1, 1, TX_FAILED, TX_STREAMING, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
  f = facts(1, 1, 1, TX_FAILED, TX_CHANNEL_CREATED, 0, 0);
  CHECK(pair_hidden_trusted(&f));
  f = facts(1, 1, 1, TX_FAILED, TX_HDL_VERIFIED, 0, 0);
  CHECK(pair_hidden_trusted(&f));
  f = facts(0, 0, 1, TX_COMPLETE, TX_NONE, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
}

TEST(pair_classify_all_states) {
  pair_facts_t f;
  f = facts(0, 0, 0, TX_NONE, TX_NONE, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_NONE);
  f = facts(1, 1, 0, TX_NONE, TX_NONE, 1, 1);
  CHECK_EQ_INT(pair_classify(&f), PAIR_COMPLETE);
  f = facts(1, 1, 1, TX_HDL_VERIFIED, TX_NONE, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_ONLY);
  f = facts(1, 1, 1, TX_STREAMING, TX_NONE, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_INCOMPLETE);
  f = facts(1, 1, 0, TX_NONE, TX_NONE, 1, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_CHANNEL_BROKEN);
  f = facts(0, 0, 0, TX_NONE, TX_NONE, 1, 1);
  CHECK_EQ_INT(pair_classify(&f), PAIR_ORPHAN_CHANNEL);
  f = facts(1, 0, 0, TX_NONE, TX_NONE, 1, 1);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_INVALID_WITH_CHANNEL);
  f = facts(1, 1, 1, TX_STREAMING, TX_NONE, 1, 1);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_INVALID_WITH_CHANNEL);
}

TEST(pair_actions_follow_plan) {
  CHECK_EQ_INT(pair_actions(PAIR_NONE), ACT_INSTALL);
  CHECK_EQ_INT(pair_actions(PAIR_COMPLETE), ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_ONLY), ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_INCOMPLETE), ACT_DELETE_INCOMPLETE | ACT_REINSTALL);
  CHECK_EQ_INT(pair_actions(PAIR_CHANNEL_BROKEN), ACT_CREATE_CHANNEL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_ORPHAN_CHANNEL), ACT_REMOVE_CHANNEL);
  /* Remove the channel before any data repair is offered. */
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_INVALID_WITH_CHANNEL), ACT_REMOVE_CHANNEL);
}

TEST(pair_labels_nonempty) {
  for (int s = PAIR_NONE; s <= PAIR_HIDDEN_INVALID_WITH_CHANNEL; s++)
    CHECK(pair_state_label((pair_state_t)s)[0] != 0);
}
