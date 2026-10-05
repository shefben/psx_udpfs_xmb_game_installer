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
  const char *jacket;     /* install_report_t.jacket */
  int verify_skipped;     /* install_report_t.verify_skipped */
  int id_on_hdd;          /* a partition with this game ID exists (any title) */
} batch_entry_t;

/* Set id_on_hdd for every entry whose game ID appears in one of the
 * partition names ("__.<PART_ID>..*" or "PP.<PART_ID>..*"), whatever the
 * title part - a game installed under another title is still installed. */
void batch_mark_on_hdd(batch_entry_t *e, int n, const char *const *names, int nnames);

/* Auto mode: wait while there is no manifest yet or the server is still
 * scanning (its manifest says scanning=1). */
int auto_should_wait(int loaded, const manifest_t *m);

/* The network comes up in the background, after the menu appears.
 * Auto-install starts on its own once (udpfsd ready, manifest with
 * auto_install, HDD usable), and only while nobody has used the menu:
 * it never interrupts someone already managing games. */
int auto_start_due(int hdd_ok, int net_ready, int manifest_loaded, int auto_install,
                   int user_acted, int already_ran);

typedef enum { AUTO_INSTALLER_OK = 0, AUTO_CREATE_INSTALLER, AUTO_STOP } auto_step_t;

/* Auto mode only creates a missing installer partition; one that exists
 * but did not mount may be damaged and is never rewritten. */
auto_step_t auto_installer_step(int exists, int mounted);

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
