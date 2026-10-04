#include "game_pair.h"

int pair_hidden_trusted(const pair_facts_t *f) {
  if (!f->hidden_exists || !f->hidden_header_valid)
    return 0;
  if (!f->has_journal)
    return 1;
  tx_state_t s = f->journal_state;
  if (s == TX_FAILED)
    s = f->journal_failed_from;
  return s >= TX_HDL_VERIFIED && s <= TX_COMPLETE;
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
  return trusted ? PAIR_HIDDEN_ONLY : PAIR_HIDDEN_INCOMPLETE;
}

unsigned pair_actions(pair_state_t s) {
  switch (s) {
  case PAIR_NONE:
    return ACT_INSTALL;
  case PAIR_COMPLETE:
  case PAIR_HIDDEN_ONLY:
    return ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE;
  case PAIR_HIDDEN_INCOMPLETE:
    return ACT_DELETE_INCOMPLETE | ACT_REINSTALL;
  case PAIR_CHANNEL_BROKEN:
    return ACT_CREATE_CHANNEL | ACT_DELETE;
  case PAIR_ORPHAN_CHANNEL:
  case PAIR_HIDDEN_INVALID_WITH_CHANNEL:
    return ACT_REMOVE_CHANNEL;
  }
  return 0;
}

const char *pair_state_label(pair_state_t s) {
  switch (s) {
  case PAIR_NONE:
    return "not installed";
  case PAIR_COMPLETE:
    return "installed";
  case PAIR_HIDDEN_ONLY:
    return "no XMB channel";
  case PAIR_HIDDEN_INCOMPLETE:
    return "INCOMPLETE copy";
  case PAIR_CHANNEL_BROKEN:
    return "channel BROKEN";
  case PAIR_ORPHAN_CHANNEL:
    return "ORPHANED channel";
  case PAIR_HIDDEN_INVALID_WITH_CHANNEL:
    return "game data INVALID";
  }
  return "?";
}
