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
  CHECK_STR(pair_untrusted_reason(&f), "game partition (__. or PP. HDL) missing");
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
  /* A complete game can drop only its channel (data kept, verified):
   * it becomes PAIR_HIDDEN_ONLY, whose Create XMB channel restores it. */
  CHECK_EQ_INT(pair_actions(PAIR_COMPLETE),
               ACT_CREATE_CHANNEL | ACT_REMOVE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_ONLY), ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_ONLY), ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE);
  /* Unverified data never gets a channel. */
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_UNVERIFIED), ACT_DELETE_INCOMPLETE | ACT_REINSTALL);
  CHECK_EQ_INT(pair_actions(PAIR_CHANNEL_BROKEN), ACT_CREATE_CHANNEL | ACT_DELETE);
  CHECK_EQ_INT(pair_actions(PAIR_ORPHAN_CHANNEL), ACT_REMOVE_CHANNEL);
  /* e.g. a game PFS-BatchKit-Manager installed: hide it, or delete it */
  CHECK_EQ_INT(pair_actions(PAIR_HIDDEN_INVALID_WITH_CHANNEL), ACT_REMOVE_CHANNEL | ACT_DELETE);
  /* A PS1 (POPStarter) game is one partition: delete only. */
  CHECK_EQ_INT(pair_actions(PAIR_PS1), ACT_DELETE);
  CHECK_STR(pair_state_label(PAIR_PS1), "PS1 game (POPStarter)");
}

TEST(pair_labels_nonempty) {
  for (int s = PAIR_NONE; s <= PAIR_HIDDEN_INVALID_WITH_CHANNEL; s++)
    CHECK(pair_state_label((pair_state_t)s)[0] != 0);
  CHECK(strstr(pair_state_label(PAIR_HIDDEN_UNVERIFIED), "UNVERIFIED") != NULL);
}

/* The DESR's partition list from Diagnostics > Dump XMB channels, plus our
 * own layouts: every game must be found once. */
TEST(pair_collect_lists_batchkit_and_own_games) {
  static const pair_part_t parts[] = {
      {"__mbr", 0x0001},
      {"__net", 0x0100},
      {"__system", 0x0100},
      {"__sysconf", 0x0100},
      {"__common", 0x0100},
      {"PP.SLUS-20066..HALF_LIFE", 0x1337},        /* PFS-BatchKit-Manager */
      {"+OPL", 0x0100},
      {"PP.SLUS-20380..JURASSIC_PARK___", 0x1337}, /* PFS-BatchKit-Manager */
      {"PP.UDPF-00001..INSTALLER", 0x0100},        /* our installer: no game */
      {"__.SLUS-20312..GRAN_TURISMO_4", 0x1337},   /* ours, hidden */
      {"PP.SLES-50330..OLD_BUILD", 0x0100},        /* earlier v2.0: PFS channel ... */
      {"__.SLES-50330..OLD_BUILD", 0x1337},        /* ... + hidden game */
      {"PP.SCUS-94600..PS1_OR_ORPHAN", 0x0100},    /* PFS channel alone */
      {"__.SLUS-21000..BOTH", 0x1337},             /* both names HDL: once */
      {"PP.SLUS-21000..BOTH", 0x1337},
  };
  pair_ref_t out[16];
  int n = pair_collect(parts, (int)(sizeof(parts) / sizeof(parts[0])), out, 16);
  CHECK_EQ_INT(n, 6);
  CHECK_STR(out[0].visible, "PP.SLUS-20066..HALF_LIFE");
  CHECK_STR(out[0].hidden, "__.SLUS-20066..HALF_LIFE");
  CHECK_STR(out[1].visible, "PP.SLUS-20380..JURASSIC_PARK___");
  CHECK_STR(out[2].hidden, "__.SLUS-20312..GRAN_TURISMO_4");
  CHECK_STR(out[3].hidden, "__.SLES-50330..OLD_BUILD");
  CHECK_STR(out[4].visible, "PP.SCUS-94600..PS1_OR_ORPHAN");
  CHECK_STR(out[5].hidden, "__.SLUS-21000..BOTH");
  CHECK_EQ_INT(pair_collect(parts, 15, out, 2), 2); /* bounded */
}
