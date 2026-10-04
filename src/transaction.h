#ifndef PSXI_TRANSACTION_H
#define PSXI_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Per-game install transaction (plan section 24). */

typedef enum {
  TX_NONE = 0,
  TX_PLANNED,
  TX_HDL_CREATED,
  TX_STREAMING,
  TX_HDL_COMPLETE,
  TX_HDL_VERIFIED,
  TX_CHANNEL_CREATED,
  TX_CHANNEL_VERIFIED,
  TX_COMPLETE,
  TX_FAILED,
  TX__COUNT
} tx_state_t;

typedef struct {
  char source_path[256];
  uint64_t source_size;
  char startup_id[16];
  char visible_partition[33];
  char hidden_partition[33];
  uint64_t bytes_expected;
  uint64_t bytes_written;
  tx_state_t state;
  tx_state_t failed_from; /* stage that failed, when state == TX_FAILED */
  char last_error[64];
} tx_journal_t;

const char *tx_state_name(tx_state_t s); /* "TX_PLANNED" ... */
int tx_state_parse(const char *name, tx_state_t *out);

/* Ordering rules. Forward moves are one step at a time; any
 * unfinished state may fail; a failed or complete transaction may be
 * restarted (TX_PLANNED) or, after the hidden game re-verifies,
 * resumed for channel repair (TX_HDL_VERIFIED). */
int tx_transition_allowed(tx_state_t from, tx_state_t to);

/* Apply a transition; returns ERR_OK or ERR_INTERNAL if forbidden. */
inst_err_t tx_advance(tx_journal_t *j, tx_state_t to);

/* Mark failure, remembering which stage failed and why. */
void tx_fail(tx_journal_t *j, inst_err_t err);

/* PP. channel creation is only permitted once the HDL data verified. */
int tx_channel_creation_allowed(const tx_journal_t *j);

/* "install-SLUS-20312.ini" (normalized partition-form game id). */
int tx_journal_filename(const char *startup_id, char out[64]);

/* key=value text, one field per line (LF). */
size_t tx_serialize(const tx_journal_t *j, char *out, size_t outsz);
/* 0 on success; -1 if malformed or a required field is missing. */
int tx_parse(const char *text, tx_journal_t *out);

#ifdef _EE
/* Persist under <dir> (e.g. "pfs0:/state"). */
inst_err_t tx_save(const char *dir, const tx_journal_t *j);
inst_err_t tx_load(const char *dir, const char *startup_id, tx_journal_t *j);
inst_err_t tx_remove(const char *dir, const char *startup_id);
/* Scan <dir> for journals not in TX_COMPLETE. Returns count (<= max). */
int tx_scan_unfinished(const char *dir, tx_journal_t *out, int max);
#endif

#endif
