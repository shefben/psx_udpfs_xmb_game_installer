#ifndef PSXI_TRANSACTION_H
#define PSXI_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Per-game install transaction (plan section 24). The journal for a
 * PP./__. pair is the authoritative record of whether its hidden game
 * data is complete and verified; nothing is stored in the HDL format. */

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
  uint64_t bytes_expected; /* logical ISO bytes to install */
  uint64_t bytes_written;  /* bytes streamed to hdl0: */
  uint64_t bytes_verified; /* bytes read back from hdl0: */
  int has_source_crc;
  uint32_t source_crc32; /* CRC-32 of every byte received from UDPFS */
  int has_installed_crc;
  uint32_t installed_crc32; /* CRC-32 of the full read-back */
  int deleting;             /* a delete of this pair was started */
  /* Identity of the hidden partition this journal describes, recorded
   * right after create+format: APA start sector, size in sectors and
   * CRC-32 of the 1 KiB HDL header. A same-named partition recreated by
   * another tool does not match, so a stale journal cannot vouch for it. */
  int has_hdl_identity;
  uint32_t hdl_start;
  uint32_t hdl_size;
  uint32_t hdl_header_crc32;
  tx_state_t state;
  tx_state_t failed_from; /* stage that failed, when state == TX_FAILED */
  char last_error[64];
  char launcher_source[24]; /* "server" | "embedded": channel KELF origin */
  char opl_cfg[8];          /* "copied" | "kept" | "failed" | "none" */
  /* The user skipped the full read-back after a complete copy (header
   * and PVD were still checked). "Verify game data" can do it later. */
  int verify_skipped;
} tx_journal_t;

const char *tx_state_name(tx_state_t s); /* "TX_PLANNED" ... */
int tx_state_parse(const char *name, tx_state_t *out);

/* Ordering rules. Forward moves are one step at a time; any
 * unfinished state may fail; a failed or complete transaction may be
 * restarted (TX_PLANNED); a channel may be (re)built (TX_HDL_VERIFIED)
 * from any journal whose data was already verified. Callers must also
 * check tx_hidden_data_verified() before a channel (re)build. */
int tx_transition_allowed(tx_state_t from, tx_state_t to);

/* Apply a transition; returns ERR_OK or ERR_INTERNAL if forbidden. */
inst_err_t tx_advance(tx_journal_t *j, tx_state_t to);

/* Mark failure, remembering which stage failed and why. */
void tx_fail(tx_journal_t *j, inst_err_t err);

/* PP. channel creation is only permitted once the HDL data verified. */
int tx_channel_creation_allowed(const tx_journal_t *j);

/* The journal proves the hidden data complete: it reached
 * TX_HDL_VERIFIED (possibly failing later, in the channel stage), all
 * expected bytes were written and read back, both CRCs are recorded
 * and equal, and no delete was started. */
int tx_hidden_data_verified(const tx_journal_t *j);
/* (With verify_skipped, the read-back is waived: the copy must still be
 * complete with its source CRC recorded.) */

/* The full read-back ran and its CRC equals the source stream's. */
int tx_hidden_data_read_back(const tx_journal_t *j);

/* Field-by-field equality (what tx_save's read-back compares). */
int tx_journal_equal(const tx_journal_t *a, const tx_journal_t *b);

/* The journal's recorded partition identity equals the given one. */
int tx_identity_matches(const tx_journal_t *j, uint32_t start, uint32_t size,
                        uint32_t header_crc32);

/* Journal file for one PP./__. pair: "install-<name without prefix>.ini",
 * e.g. "install-SLUS-20312..GRAN_TURISMO_4.ini". Same result for either
 * member of the pair. -1 if `partition` is not a game partition. */
int tx_journal_filename_for(const char *partition, char out[96]);

/* key=value text, one field per line (LF). */
size_t tx_serialize(const tx_journal_t *j, char *out, size_t outsz);
/* 0 on success; -1 if malformed or a required field is missing. */
int tx_parse(const char *text, tx_journal_t *out);

#ifdef _EE
/* Persist under <dir> (e.g. "pfs0:/state"), one file per pair
 * (tx_journal_filename_for). `partition` may be either pair member.
 * tx_save writes <file>.tmp then renames it over <file>. */
inst_err_t tx_save(const char *dir, const tx_journal_t *j);
inst_err_t tx_load(const char *dir, const char *partition, tx_journal_t *j);
inst_err_t tx_remove(const char *dir, const char *partition);
/* Scan <dir> for journals not in TX_COMPLETE. Returns count (<= max). */
int tx_scan_unfinished(const char *dir, tx_journal_t *out, int max);
#endif

#endif
