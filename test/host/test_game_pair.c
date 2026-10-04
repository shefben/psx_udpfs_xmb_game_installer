#include "../../src/game_pair.h"
#include "test.h"

static pair_facts_t facts(int he, int hv, int has_j, int j_verified, int ve, int vv) {
  pair_facts_t f;
  memset(&f, 0, sizeof(f));
  f.hidden_exists = he;
  f.hidden_header_valid = hv;
  f.has_journal = has_j;
  f.journal_verified = j_verified;
  f.visible_exists = ve;
  f.visible_valid = vv;
  return f;
}

TEST(pair_trust_requires_verified_journal) {
  pair_facts_t f;
  f = facts(1, 1, 1, 1, 0, 0);
  CHECK(pair_hidden_trusted(&f));
  /* No journal (lost, or a game from another tool): UNVERIFIED. */
  f = facts(1, 1, 0, 0, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
  /* Journal that never verified (power loss mid-copy, failed copy). */
  f = facts(1, 1, 1, 0, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
  /* Header must still be valid. */
  f = facts(1, 0, 1, 1, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
  f = facts(0, 0, 1, 1, 0, 0);
  CHECK(!pair_hidden_trusted(&f));
}

TEST(pair_classify_all_states) {
  pair_facts_t f;
  f = facts(0, 0, 0, 0, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_NONE);
  f = facts(1, 1, 1, 1, 1, 1);
  CHECK_EQ_INT(pair_classify(&f), PAIR_COMPLETE);
  f = facts(1, 1, 1, 1, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_ONLY);
  f = facts(1, 1, 0, 0, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_UNVERIFIED);
  f = facts(1, 1, 1, 0, 0, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_UNVERIFIED);
  f = facts(1, 1, 1, 1, 1, 0);
  CHECK_EQ_INT(pair_classify(&f), PAIR_CHANNEL_BROKEN);
  f = facts(0, 0, 0, 0, 1, 1);
  CHECK_EQ_INT(pair_classify(&f), PAIR_ORPHAN_CHANNEL);
  f = facts(1, 1, 0, 0, 1, 1); /* foreign/unverified data behind a channel */
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_INVALID_WITH_CHANNEL);
}

TEST(pair_actions_follow_plan) {
  CHECK_EQ_INT(pair_actions(PAIR_NONE), ACT_INSTALL);
  CHECK_EQ_INT(pair_actions(PAIR_COMPLETE), ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_ONLY), ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  /* Unverified data never gets a channel. */
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_UNVERIFIED), ACT_DELETE_INCOMPLETE | ACT_REINSTALL);
  CHECK_EQ_INT(pair_actions(PAIR_CHANNEL_BROKEN), ACT_CREATE_CHANNEL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_ORPHAN_CHANNEL), ACT_REMOVE_CHANNEL);
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_INVALID_WITH_CHANNEL), ACT_REMOVE_CHANNEL);
}

TEST(pair_labels_nonempty) {
  for (int s = PAIR_NONE; s <= PAIR_HIDDEN_INVALID_WITH_CHANNEL; s++)
    CHECK(pair_state_label((pair_state_t)s)[0] != 0);
  CHECK(strstr(pair_state_label(PAIR_HIDDEN_UNVERIFIED), "UNVERIFIED") != NULL);
}
