#include "../../src/game_pair.h"
#include "test.h"

static pair_facts_t facts(int he, int hv, int has_j, int j_verified, int ve, int vv) {
  pair_facts_t f;
  memset(&f, 0, sizeof(f));
  f.hidden_exists = he;
  f.hidden_header_valid = hv;
  f.has_journal = has_j;
  f.journal_verified = j_verified;
  f.journal_matches_partition = has_j;
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

/* Review finding 2: a stale journal for a same-named partition that was
 * recreated by another tool must not make the new data trusted. */
TEST(pair_trust_requires_journal_bound_to_partition) {
  pair_facts_t f = facts(1, 1, 1, 1, 0, 0);
  f.journal_matches_partition = 0;
  CHECK(!pair_hidden_trusted(&f));
  CHECK_EQ_INT(pair_classify(&f), PAIR_HIDDEN_UNVERIFIED);
}

TEST(pair_untrusted_reason_names_first_failed_check) {
  pair_facts_t f = facts(1, 1, 1, 1, 1, 1);
  CHECK(pair_untrusted_reason(&f) == NULL);
  f = facts(0, 0, 0, 0, 1, 1);
  CHECK_STR(pair_untrusted_reason(&f), "game data partition (__.) missing");
  f = facts(1, 0, 1, 1, 1, 1);
  CHECK_STR(pair_untrusted_reason(&f), "game data header unreadable or not HDL");
  f = facts(1, 1, 0, 0, 1, 1);
  CHECK_STR(pair_untrusted_reason(&f), "no install journal found for this game");
  f = facts(1, 1, 1, 0, 1, 1);
  CHECK_STR(pair_untrusted_reason(&f), "journal: copy not completed and CRC-verified");
  f = facts(1, 1, 1, 1, 1, 1);
  f.journal_matches_partition = 0;
  CHECK_STR(pair_untrusted_reason(&f),
            "partition start/size/header differ from the journal");
}

TEST(pair_label_marks_skipped_verification) {
  pair_facts_t f = facts(1, 1, 1, 1, 1, 1);
  CHECK_STR(pair_label(&f), "installed");
  CHECK(pair_can_verify(&f));
  f.verify_skipped = 1;
  CHECK_STR(pair_label(&f), "installed, NOT VERIFIED");
  CHECK(pair_can_verify(&f));
  f = facts(1, 1, 1, 1, 0, 0);
  f.verify_skipped = 1;
  CHECK_STR(pair_label(&f), "not verified, channel pending");
  f = facts(1, 1, 0, 0, 0, 0); /* no journal: nothing to compare with */
  CHECK(!pair_can_verify(&f));
  f = facts(1, 1, 1, 0, 1, 1); /* copy never completed */
  CHECK(!pair_can_verify(&f));
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
