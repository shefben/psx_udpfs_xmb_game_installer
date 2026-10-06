#include <string.h>

#include "game_pair.h"
#include "partname.h"
#include "util.h"

int pair_hidden_trusted(const pair_facts_t *f) {
  return f->hidden_exists && f->hidden_header_valid && f->has_journal &&
         f->journal_verified && f->journal_matches_partition;
}

const char *pair_untrusted_reason(const pair_facts_t *f) {
  if (!f->hidden_exists)
    return "game partition (__. or PP. HDL) missing";
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
    return ACT_CREATE_CHANNEL | ACT_REMOVE_CHANNEL | ACT_REINSTALL | ACT_DELETE;
  case PAIR_HIDDEN_ONLY:
    return ACT_CREATE_CHANNEL | ACT_REINSTALL | ACT_DELETE;
  case PAIR_HIDDEN_UNVERIFIED:
    return ACT_DELETE_INCOMPLETE | ACT_REINSTALL;
  case PAIR_CHANNEL_BROKEN:
    return ACT_CREATE_CHANNEL | ACT_DELETE;
  case PAIR_ORPHAN_CHANNEL:
    return ACT_REMOVE_CHANNEL;
  case PAIR_HIDDEN_INVALID_WITH_CHANNEL:
    return ACT_REMOVE_CHANNEL | ACT_DELETE;
  case PAIR_PS1:
    return ACT_DELETE;
  }
  return 0;
}

const char *pair_label(const pair_facts_t *f) {
  pair_state_t s = pair_classify(f);
  if (f->verify_skipped && s == PAIR_COMPLETE)
    return "installed, NOT VERIFIED";
  if (f->verify_skipped && s == PAIR_HIDDEN_ONLY)
    return "not verified, channel pending";
  if (s == PAIR_COMPLETE && f->legacy_channel)
    return "installed, with cover";
  return pair_state_label(s);
}

int pair_can_add_cover(const pair_facts_t *f) {
  return pair_classify(f) == PAIR_COMPLETE && f->data_visible && f->visible_valid;
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
  case PAIR_PS1:
    return "PS1 game (POPStarter)";
  }
  return "?";
}

int pair_collect(const pair_part_t *parts, int np, pair_ref_t *out, int max) {
  int n = 0;
  for (int i = 0; i < np && n < max; i++) {
    const char *name = parts[i].name;
    char partner[APA_NAME_MAX + 1];
    if ((partition_is_hidden_game(name) || partition_is_game_channel(name)) &&
        parts[i].type == APA_TYPE_HDL_ID) {
      int dup = 0;
      for (int k = 0; k < n; k++)
        dup |= !strcmp(out[k].visible + 3, name + 3);
      if (dup)
        continue;
      partition_partner(name, partner);
      str_copy(out[n].hidden, name[0] == '_' ? name : partner, sizeof(out[n].hidden));
      str_copy(out[n].visible, name[0] == '_' ? partner : name, sizeof(out[n].visible));
      n++;
    } else if (partition_is_xmb_channel(name, parts[i].type)) {
      partition_partner(name, partner);
      int have = 0;
      for (int k = 0; k < np; k++)
        have |= !strcmp(parts[k].name, partner);
      if (!have) {
        str_copy(out[n].visible, name, sizeof(out[n].visible));
        str_copy(out[n].hidden, partner, sizeof(out[n].hidden));
        n++;
      }
    }
  }
  return n;
}
