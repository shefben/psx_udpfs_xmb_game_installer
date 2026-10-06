#ifndef PSXI_XMB_GAME_CHANNEL_H
#define PSXI_XMB_GAME_CHANNEL_H

#include <stdint.h>

#include "errors.h"
#include "game_pair.h"
#include "xmb_text.h"
#include "hdl_plan.h"
#include "iso9660.h"
#include "manifest.h"
#include "opl_launcher_payload.h"
#include "partname.h"
#include "source.h"

/* Per-game install / repair / delete (plan sections 24-28). */

typedef struct {
  char source_path[SOURCE_PATH_MAX];
  source_type_t type;
  iso_info_t iso;
  char title[64];
  char visible[APA_NAME_MAX + 1];
  char hidden[APA_NAME_MAX + 1];
  hdl_alloc_t alloc;
  /* Continue this plan's interrupted copy from its journal checkpoint
   * instead of creating the partitions (game_install). */
  int resume;
} game_plan_t;

typedef enum {
  STAGE_PREPARING = 0,
  STAGE_CHECKING_RESUME, /* re-reading the last copied segments */
  STAGE_CREATING_HDL,
  STAGE_COPYING,
  STAGE_VALIDATING,
  STAGE_CREATING_CHANNEL,
  STAGE_FINISHED,
} install_stage_t;

const char *install_stage_name(install_stage_t s);

typedef struct {
  void (*stage)(void *ctx, install_stage_t s);
  void (*progress)(void *ctx, uint64_t done, uint64_t total, uint32_t elapsed_s);
  int (*should_abort)(void *ctx);
  void *ctx;
  /* After should_abort() stopped the read-back: non-zero if that was
   * the user skipping verification, not aborting the install. */
  int (*skip_verify)(void *ctx);
  /* After should_abort() stopped the copy: non-zero if the user paused
   * (the copy can be resumed), not aborted. */
  int (*paused)(void *ctx);
} install_ui_t;

typedef struct {
  inst_err_t err;
  int rc;
  install_stage_t stage;
  const char *detail;
  int hidden_exists;
  int visible_exists;
  int data_installed_no_channel; /* stopped at TX_HDL_VERIFIED (OPL) */
  /* Integrity results, shown on the finish/error screen. */
  int have_crc;
  uint32_t source_crc32;
  uint32_t installed_crc32;
  uint64_t bytes_written;
  uint64_t bytes_verified;
  int verify_skipped; /* the user skipped the full read-back */
  uint64_t resumed_from; /* resume: bytes already on the HDD before this run */
  int resume_checked;    /* resume: checkpoint segments read back */
  /* OPL per-game cfg from the server: "copied" | "kept" | "failed" |
   * "none"; NULL when no channel was built. */
  const char *opl_cfg;
  /* Channel cover: "server" (udpfsd's prepared jacket or an image next
   * to the game) | "missing" (the server listed a cover that could not
   * be read; default used) | "default"; NULL when no channel was built. */
  const char *jacket;
} install_report_t;

/* Probe a UDPFS file and build names/sizes. Leaves no source open. */
inst_err_t game_plan_build(const char *path, game_plan_t *p, int *rc_out);

/* The image is on udpfsd (needs the network), not e.g. on USB. */
int game_source_is_server(const char *path);

/* Rebuild partition names after the title was edited. */
inst_err_t game_plan_set_title(game_plan_t *p, const char *title);

/* Plan from a udpfsd manifest entry without opening the image (size,
 * disc type and title from the server). game_install() still re-probes
 * the image, refuses a different game ID or size, and then uses the
 * PS2's own probe result. */
inst_err_t game_plan_from_manifest(const manifest_entry_t *m, game_plan_t *p);

/* Plan to continue the interrupted copy of `hidden`: re-probes the
 * image at the journal's source path (server must be up) and sets
 * p->resume. game_install() re-checks everything before writing. */
inst_err_t game_resume_plan(const char *hidden, game_plan_t *p, int *rc_out);

/* XMB game info (release date, developer, publisher, genre) that udpfsd
 * prepared from its game database; 0 if none (server down, no entry). */
int game_load_info(const char *boot_id, xmb_game_info_t *gi);

/* XMB covers for a game: udpfsd's prepared pair (jkt/<ID>_L.png 140x200
 * and jkt/<ID>.png 74x108), used only at exactly those sizes; else the
 * built-in default pair. owned[0..1] (if set) must be freed. Returns
 * where they came from: "server" | "missing" | "default". */
const char *game_load_jackets(const char *boot_id, jacket_pair_t *j, void *owned[2]);

/* The game's HDL partition for the pair key `hidden` ("__.X"): "__.X"
 * while it is copied or hidden (returns 0), "PP.X" once it is shown in
 * the XMB (returns 1); -1 if neither exists as HDL (out = hidden). */
int game_data_partition(const char *hidden, char out[APA_NAME_MAX + 1]);

/* Give a shown game an XMB cover (experimental): PFS-BatchKit-Manager's
 * resource-partition layout, PFS PP.X with res/ + the game hidden as
 * __.X. Rebuild XMB channel undoes it. */
void game_add_cover(const char *hidden, const install_ui_t *ui, install_report_t *rep);

/* Change the XMB title of a shown game (its boot header's icon.sys). */
inst_err_t game_set_title(const char *hidden, const char *title, int *rc_out);

/* Gather on-disk facts for a pair (exists/valid/journal). */
void game_pair_facts(const char *visible, const char *hidden, pair_facts_t *f);

/* Read back a completed install whose verification was skipped (or
 * re-check any install) and compare with the source CRC in its journal.
 * Records the result in the journal: a match clears verify_skipped; a
 * mismatch marks the data as not trusted (journal fails). */
void game_verify_data(const char *hidden, const install_ui_t *ui, install_report_t *rep);

/* Every fact behind the pair's state (partitions, journal fields,
 * identity), as text for the Details screen. Returns the length. */
size_t game_pair_details(const char *visible, const char *hidden, char *out, size_t outsz);

/* Full install in plan order. Preconditions checked inside. If
 * `allow_without_opl` the data is copied even when the OPL runtime is
 * missing and the run stops at TX_HDL_VERIFIED. */
void game_install(game_plan_t *p, int allow_without_opl,
                  const install_ui_t *ui, install_report_t *rep);

/* Show a verified game in the XMB, or repair/convert its entry: writes
 * the boot header into the game partition (the game data and HDL header
 * stay untouched), removes an older release's PFS channel and renames
 * __.X to PP.X. */
void game_create_channel(const char *hidden, const install_ui_t *ui,
                         install_report_t *rep);

/* Delete in the safe order: mark the journal `deleting` (the data is
 * untrusted from here on), PP. (verify gone), __. (verify gone), then
 * remove the journal. Either name may be absent. */
inst_err_t game_delete_pair(const char *visible, const char *hidden,
                            const char **failed_name, int *rc_out);

/* Take a game out of the XMB, keeping it: a shown game partition is
 * renamed back to __.X; an older release's PFS channel is removed. The
 * game and its journal stay as they are (still verified), so Create XMB channel
 * restores the channel without copying again. */
inst_err_t game_remove_channel(const char *visible, int *rc_out);

#endif
