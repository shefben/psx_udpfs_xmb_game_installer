#include "game_pair.h"

int pair_hidden_trusted(const pair_facts_t *f) {
  return f->hidden_exists && f->hidden_header_valid && f->has_journal &&
         f->journal_verified && f->journal_matches_partition;
}

const char *pair_untrusted_reason(const pair_facts_t *f) {
  if (!f->hidden_exists)
    return "game data partition (__.) missing";
  if (!f->hidden_header_valid)
    return "game data header unreadable or not HDL";
  if (!f->has_journal)
    return "no install journal found for this game";
  if (!f->journal_verified)
    return "journal: copy not completed and CRC-verified";
  if (!f->journal_matches_partition)
    return "partition start/size/header differ from the journal";
  return NULL;
}

pair_state_t pair_classify(const pair_facts_t *f) {
  int trusted = pair_hidden_trusted(f);
  if (!f->hidden_exists && !f->visible_exists)
    return PAIR_NONE;
  if (f->visible_exists) {
    if (!f->hidden_exists)
      return PAIR_ORPHAN_CHANNEL;
    if (!trusted)
      return PAIR_HIDDEN_INVALID_WITH_CHANNEL;
    return f->visible_valid ? PAIR_COMPLETE : PAIR_CHANNEL_BROKEN;
  }
  return trusted ? PAIR_HIDDEN_ONLY : PAIR_HIDDEN_UNVERIFIED;
}

unsigned pair_actions(pair_state_t s) {
  switch (s) {
  case PAIR_NONE:
    return ACT_INSTALL;
  case PAIR_COMPLETE:
  case PAIR_HIDDEN_ONLY:
    return ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE;
  case PAIR_HIDDEN_UNVERIFIED:
    return ACT_DELETE_INCOMPLETE | ACT_REINSTALL;
  case PAIR_CHANNEL_BROKEN:
    return ACT_CREATE_CHANNEL | ACT_DELETE;
  case PAIR_ORPHAN_CHANNEL:
  case PAIR_HIDDEN_INVALID_WITH_CHANNEL:
    return ACT_REMOVE_CHANNEL;
  }
  return 0;
}

const char *pair_label(const pair_facts_t *f) {
  pair_state_t s = pair_classify(f);
  if (f->verify_skipped && s == PAIR_COMPLETE)
    return "installed, NOT VERIFIED";
  if (f->verify_skipped && s == PAIR_HIDDEN_ONLY)
    return "not verified, channel pending";
  return pair_state_label(s);
}

int pair_can_verify(const pair_facts_t *f) { return pair_hidden_trusted(f); }

const char *pair_state_label(pair_state_t s) {
  switch (s) {
  case PAIR_NONE:
    return "not installed";
  case PAIR_COMPLETE:
    return "installed";
  case PAIR_HIDDEN_ONLY:
    return "verified, channel pending";
  case PAIR_HIDDEN_UNVERIFIED:
    return "UNKNOWN/UNVERIFIED data";
  case PAIR_CHANNEL_BROKEN:
    return "channel BROKEN";
  case PAIR_ORPHAN_CHANNEL:
    return "ORPHANED channel";
  case PAIR_HIDDEN_INVALID_WITH_CHANNEL:
    return "channel on UNVERIFIED data";
  }
  return "?";
}
