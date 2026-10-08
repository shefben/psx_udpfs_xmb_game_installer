#ifndef PSXI_GAME_PAIR_H
#define PSXI_GAME_PAIR_H

#include "transaction.h"

/* Classification of a PP./__. pair (plan sections 26-28). Pure. */

typedef enum {
  PAIR_NONE = 0,             /* neither partition exists */
  PAIR_COMPLETE,             /* verified hidden game + valid channel */
  PAIR_HIDDEN_ONLY,          /* verified hidden game, no channel */
  PAIR_HIDDEN_UNVERIFIED,    /* hidden exists, no trustworthy journal, no PP */
  PAIR_CHANNEL_BROKEN,       /* verified hidden game, PP invalid */
  PAIR_ORPHAN_CHANNEL,       /* PP exists, hidden missing */
  PAIR_HIDDEN_INVALID_WITH_CHANNEL, /* PP exists, hidden not verified */
  PAIR_PS1, /* PP holds a PS1 game (IMAGE0.VCD, no __. partner); set by the scan */
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
  int has_journal;         /* a journal for exactly this pair exists */
  int journal_verified;    /* tx_hidden_data_verified() on that journal */
  int journal_matches_partition; /* tx_identity_matches() on the live partition */
  int visible_exists; /* in the XMB: PP. game partition, or an old PFS channel */
  int visible_valid;  /* PP. game partition with a complete boot header */
  int data_visible;   /* the HDL game partition itself is PP.X (BatchKit layout) */
  int legacy_channel; /* PFS PP.X next to __.X: a cover partition (or an older
                       * release's channel) */
  int cover_missing; /* PFS jackets missing, invalid, default or unreadable */
  int verify_skipped; /* journal: full read-back skipped by the user */
  int resumable;      /* interrupted copy, checkpoint bound to this partition */
  uint64_t resume_bytes; /* bytes already copied (when resumable) */
} pair_facts_t;

/* Hidden data is trusted only when its header is valid AND this
 * installer's journal for the exact pair records a completed,
 * CRC-verified copy. hdlfs writes the header at format time, before
 * any data, so a header proves nothing about the data. Without such a
 * journal (interrupted copy, lost journal, game from another tool) the
 * data is UNKNOWN/UNVERIFIED. */
int pair_hidden_trusted(const pair_facts_t *f);

/* First check pair_hidden_trusted() fails, as text; NULL if trusted. */
const char *pair_untrusted_reason(const pair_facts_t *f);

pair_state_t pair_classify(const pair_facts_t *f);

/* Bitmask of pair_action_t allowed in a state. */
unsigned pair_actions(pair_state_t s);

const char *pair_state_label(pair_state_t s);

/* pair_state_label, plus "not verified" for a trusted install whose
 * read-back was skipped. */
const char *pair_label(const pair_facts_t *f);

/* Offer "Verify game data": a completed install with a journal bound to
 * the partition (verified or skipped). */
int pair_can_verify(const pair_facts_t *f);

/* "Add XMB cover": a complete game shown as one HDL partition whose boot
 * header is intact (a game with a cover partition already has one). */
int pair_can_add_cover(const pair_facts_t *f);

/* One partition of the APA list. */
typedef struct {
  const char *name;
  unsigned type;
} pair_part_t;

/* One game for Installed Games / Remove Games: its two names. */
typedef struct {
  char visible[33]; /* PP.X */
  char hidden[33];  /* __.X (the key of its journal) */
} pair_ref_t;

/* Games on the HDD, from the APA list, in list order:
 *  - an HDL partition named __.X or PP.X (hidden, or shown in the XMB
 *    as PFS-BatchKit-Manager and this installer install games), listed
 *    once even if both names exist;
 *  - a PFS PP.X channel without a __.X partner (an orphaned channel of
 *    an older release, or a PS1 game).
 * A PFS PP.X next to its __.X belongs to that game. Returns the count. */
int pair_collect(const pair_part_t *parts, int np, pair_ref_t *out, int max);

#endif
