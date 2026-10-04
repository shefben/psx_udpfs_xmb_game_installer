#ifndef PSXI_XMB_GAME_CHANNEL_H
#define PSXI_XMB_GAME_CHANNEL_H

#include <stdint.h>

#include "errors.h"
#include "game_pair.h"
#include "hdl_plan.h"
#include "iso9660.h"
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
} game_plan_t;

typedef enum {
  STAGE_PREPARING = 0,
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
} install_report_t;

/* Probe a UDPFS file and build names/sizes. Leaves no source open. */
inst_err_t game_plan_build(const char *path, game_plan_t *p, int *rc_out);

/* Rebuild partition names after the title was edited. */
inst_err_t game_plan_set_title(game_plan_t *p, const char *title);

/* Gather on-disk facts for a pair (exists/valid/journal). */
void game_pair_facts(const char *visible, const char *hidden, pair_facts_t *f);

/* Full install in plan order. Preconditions checked inside. If
 * `allow_without_opl` the data is copied even when the OPL runtime is
 * missing and the run stops at TX_HDL_VERIFIED. */
void game_install(game_plan_t *p, int allow_without_opl,
                  const install_ui_t *ui, install_report_t *rep);

/* Create or rebuild only the PP. channel for a verified hidden game.
 * Never touches the hidden partition. */
void game_create_channel(const char *hidden, const install_ui_t *ui,
                         install_report_t *rep);

/* Delete in the safe order: mark the journal `deleting` (the data is
 * untrusted from here on), PP. (verify gone), __. (verify gone), then
 * remove the journal. Either name may be absent. */
inst_err_t game_delete_pair(const char *visible, const char *hidden,
                            const char **failed_name, int *rc_out);

#endif
