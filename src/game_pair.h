#ifndef PSXI_GAME_PAIR_H
#define PSXI_GAME_PAIR_H

#include "transaction.h"

/* Classification of a PP./__. pair (plan sections 26-28). Pure. */

typedef enum {
  PAIR_NONE = 0,             /* neither partition exists */
  PAIR_COMPLETE,             /* valid hidden game + valid channel */
  PAIR_HIDDEN_ONLY,          /* valid hidden game, no channel */
  PAIR_HIDDEN_INCOMPLETE,    /* hidden exists but data not trusted, no PP */
  PAIR_CHANNEL_BROKEN,       /* valid hidden game, PP invalid */
  PAIR_ORPHAN_CHANNEL,       /* PP exists, hidden missing */
  PAIR_HIDDEN_INVALID_WITH_CHANNEL, /* PP exists, hidden untrusted */
} pair_state_t;

typedef enum {
  ACT_INSTALL = 1 << 0,
  ACT_CREATE_CHANNEL = 1 << 1, /* create/repair/rebuild PP only */
  ACT_REINSTALL = 1 << 2,      /* delete pair, copy again */
  ACT_DELETE = 1 << 3,         /* delete pair (PP first) */
  ACT_DELETE_INCOMPLETE = 1 << 4,
  ACT_REMOVE_CHANNEL = 1 << 5, /* remove PP only */
} pair_action_t;

typedef struct {
  int hidden_exists;
  int hidden_header_valid; /* HDL type + parsable header + id + title */
  int hidden_marker;       /* HDL_MARK_* from the header */
  int has_journal;
  tx_state_t journal_state;
  tx_state_t journal_failed_from;
  int visible_exists;
  int visible_valid; /* files + PPAA header verified */
} pair_facts_t;

/* Hidden data is trusted when the header is valid and the journal (if
 * any) shows the copy reached TX_HDL_VERIFIED. A header alone is not
 * enough: hdlfs writes it at format time, before any data is copied.
 * Without a journal (games from other tools) the header decides. */
int pair_hidden_trusted(const pair_facts_t *f);

pair_state_t pair_classify(const pair_facts_t *f);

/* Bitmask of pair_action_t allowed in a state. */
unsigned pair_actions(pair_state_t s);

const char *pair_state_label(pair_state_t s);

#endif
