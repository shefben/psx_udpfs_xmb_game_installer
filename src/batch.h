#ifndef PSXI_BATCH_H
#define PSXI_BATCH_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "game_pair.h"
#include "manifest.h"
#include "partname.h"
#include "source.h"

/* Batch install of every game udpfsd lists in its manifest (all game
 * folders of udpfsd.cfg), or, with a server without a manifest, of every
 * game in udpfs:/INSTALL. Pure selection/summary logic; the console flow
 * is in flows.c. */

#define BATCH_DIR "udpfs:/INSTALL"
#define BATCH_MAX 256

typedef enum {
  BATCH_ELIGIBLE = 0,  /* valid PS2 image, pair free */
  BATCH_INVALID,       /* probe failed (not a PS2 ISO, read error...) */
  BATCH_EXISTS,        /* pair already on the HDD (any state) */
  BATCH_DUPLICATE,     /* same partition pair as an earlier entry */
  BATCH_TOO_BIG,       /* does not fit the drive's APA limits */
  BATCH_NO_SPACE,      /* auto mode: does not fit the remaining free space */
} batch_status_t;

typedef enum {
  BATCH_PENDING = 0,
  BATCH_DONE,     /* TX_COMPLETE */
  BATCH_DATA_ONLY, /* verified data, channel pending (OPL missing) */
  BATCH_FAILED,
  BATCH_SKIPPED,  /* not run (batch stopped) */
} batch_result_t;

typedef struct {
  char path[SOURCE_PATH_MAX];
  char name[96];          /* file name for display */
  source_type_t type;
  inst_err_t probe_err;   /* ERR_OK when probed fine */
  char boot_id[16];
  char title[64];
  char visible[APA_NAME_MAX + 1];
  char hidden[APA_NAME_MAX + 1];
  uint64_t bytes;         /* logical ISO bytes */
  uint32_t alloc_mb;      /* APA data partitions, excluding the channel */
  pair_state_t pair;      /* state on the HDD before the batch */
  batch_status_t status;
  int selected;
  int duplicate_of;       /* index, when status == BATCH_DUPLICATE */
  batch_result_t result;
  inst_err_t err;
  const char *stage;
  const char *opl_cfg;    /* install_report_t.opl_cfg */
} batch_entry_t;

/* Classify entries (probe results and pair states already filled):
 * invalid -> EXISTS (pair present) -> DUPLICATE (same hidden name as an
 * earlier eligible entry) -> ELIGIBLE. Selects every eligible entry. */
void batch_classify(batch_entry_t *e, int n);

/* Fill an entry (path, name, type, size, ID, title, probe result) from a
 * manifest entry; partition names and pair state are filled by the caller. */
void batch_entry_from_manifest(batch_entry_t *e, const manifest_entry_t *m);

/* Toggle selection; only eligible entries can be selected. Returns the
 * new selected state (0 for ineligible). */
int batch_toggle(batch_entry_t *e);

int batch_count_selected(const batch_entry_t *e, int n);

/* MiB the selected entries need on the HDD (data + 128 MiB channel each). */
uint64_t batch_needed_mb(const batch_entry_t *e, int n);

/* Auto mode: select eligible entries in list order while their total
 * need (data + 128 MiB channel each) fits free_mb; eligible entries that
 * do not fit become BATCH_NO_SPACE. Returns the number selected. */
int batch_auto_select(batch_entry_t *e, int n, uint64_t free_mb);

const char *batch_status_label(batch_status_t s);
const char *batch_result_label(batch_result_t r);

/* One-line row for the selection list. */
void batch_format_row(const batch_entry_t *e, char *out, size_t outsz);

/* Summary text: counts, then one line per entry that was selected. */
size_t batch_summary(const batch_entry_t *e, int n, char *out, size_t outsz);

#endif
