#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "hdd_partitions.h"
#include "hdl_header.h"
#include "hdl_install.h"
#include "opl_dependency.h"
#include "opl_launcher_payload.h"
#include "pfs_channel.h"
#include "source_udpfs.h"
#include "transaction.h"
#include "util.h"
#include "xmb_game_channel.h"
#include "xmb_text.h"

#define JACKET_MAX (512 * 1024)

const char *install_stage_name(install_stage_t s) {
  switch (s) {
  case STAGE_PREPARING:
    return "preparing";
  case STAGE_CREATING_HDL:
    return "creating HDL";
  case STAGE_COPYING:
    return "copying game";
  case STAGE_VALIDATING:
    return "validating";
  case STAGE_CREATING_CHANNEL:
    return "creating XMB channel";
  case STAGE_FINISHED:
    return "finished";
  }
  return "?";
}

static GameSource g_src;
static udpfs_src_t g_usrc;

inst_err_t game_plan_build(const char *path, game_plan_t *p, int *rc_out) {
  memset(p, 0, sizeof(*p));
  *rc_out = 0;
  str_copy(p->source_path, path, sizeof(p->source_path));
  p->type = source_classify(path);

  source_udpfs_init(&g_src, &g_usrc);
  inst_err_t e = source_open(&g_src, path);
  if (!e)
    e = iso_probe(&g_src, iso_hint_from_path(path), &p->iso);
  *rc_out = g_src.last_rc;
  source_close(&g_src);
  if (e)
    return e;

  char title[64];
  default_display_title(&p->iso, path, title, sizeof(title));
  if (game_plan_set_title(p, title))
    return ERR_SOURCE_SYSTEM_CNF;
  return hdl_plan_alloc((uint64_t)p->iso.sectors * ISO_SECTOR, &p->alloc);
}

inst_err_t game_plan_set_title(game_plan_t *p, const char *title) {
  char clean[64];
  xmb_sanitize_value(title, clean, sizeof(clean));
  if (!clean[0])
    str_copy(clean, p->iso.part_id, sizeof(clean));
  str_copy(p->title, clean, sizeof(p->title));
  if (build_game_partition_pair(p->iso.boot_id, p->title, p->visible, p->hidden))
    return ERR_INVALID_ARG;
  return ERR_OK;
}

/* Load the journal belonging to exactly this pair, if any. */
static int load_pair_journal(const char *hidden, tx_journal_t *j) {
  char id[16];
  if (!g_app.app_mounted || part_id_from_partition(hidden, id) < 0)
    return 0;
  if (tx_load(APP_STATE_DIR, id, j) != ERR_OK)
    return 0;
  return strcmp(j->hidden_partition, hidden) == 0;
}

void game_pair_facts(const char *visible, const char *hidden, pair_facts_t *f) {
  memset(f, 0, sizeof(*f));
  hdl_header_info_t h;
  f->hidden_exists = hdd_exists(hidden) > 0;
  if (f->hidden_exists)
    f->hidden_header_valid = hdl_partition_looks_valid(hidden, &h);
  tx_journal_t j;
  if (load_pair_journal(hidden, &j)) {
    f->has_journal = 1;
    f->journal_state = j.state;
    f->journal_failed_from = j.failed_from;
  }
  f->visible_exists = hdd_exists(visible) > 0;
  if (f->visible_exists)
    f->visible_valid = channel_quick_check(visible) == ERR_OK;
}

/* Jacket: udpfs:/ART/<BOOT_ID>.png, then <source>.png, then default. */
static void load_jacket(const char *boot_id, const char *source_path,
                        const uint8_t **data, uint32_t *size, void **owned) {
  char path[SOURCE_PATH_MAX + 8];
  *owned = NULL;
  if (g_app.net == NETWORK_READY) {
    const char *cands[2] = {path, NULL};
    snprintf(path, sizeof(path), "udpfs:/ART/%s.png", boot_id);
    char alt[SOURCE_PATH_MAX + 8];
    if (source_path && source_path[0]) {
      snprintf(alt, sizeof(alt), "%s.png", source_path);
      cands[1] = alt;
    }
    for (int i = 0; i < 2; i++) {
      void *buf = NULL;
      if (!cands[i])
        continue;
      int n = file_load(cands[i], &buf, JACKET_MAX);
      if (n > 0 && png_basic_valid(buf, (uint32_t)n)) {
        *data = buf;
        *size = (uint32_t)n;
        *owned = buf;
        return;
      }
      free(buf);
    }
  }
  payload_default_jacket(data, size);
}

static void journal_save(tx_journal_t *j) {
  if (g_app.app_mounted)
    tx_save(APP_STATE_DIR, j);
}

static int advance(tx_journal_t *j, tx_state_t s) {
  if (tx_advance(j, s) != ERR_OK)
    return -1;
  journal_save(j);
  return 0;
}

static void stage(const install_ui_t *ui, install_report_t *rep, install_stage_t s) {
  rep->stage = s;
  if (ui && ui->stage)
    ui->stage(ui->ctx, s);
}

static void finish_report(install_report_t *rep, const char *visible,
                          const char *hidden) {
  rep->hidden_exists = hdd_exists(hidden) > 0;
  rep->visible_exists = hdd_exists(visible) > 0;
}

/* Build the channel for a verified hidden game; journal must be at
 * TX_HDL_VERIFIED. Shared by install and repair. */
static void build_channel(tx_journal_t *j, const char *title,
                          const install_ui_t *ui, install_report_t *rep) {
  payload_t kelf;
  stage(ui, rep, STAGE_CREATING_CHANNEL);
  if ((rep->err = payload_opl_launcher(&kelf, g_app.app_mounted,
                                       g_app.net == NETWORK_READY))) {
    rep->detail = "OPL-Launcher EXECUTE.KELF";
    tx_fail(j, rep->err);
    journal_save(j);
    return;
  }
  char info[1024];
  uint32_t info_len = (uint32_t)xmb_game_info_sys(info, sizeof(info), title,
                                                  j->startup_id);
  const uint8_t *jkt;
  uint32_t jkt_size;
  void *jkt_owned;
  load_jacket(j->startup_id, j->source_path, &jkt, &jkt_size, &jkt_owned);

  channel_content_t c = {kelf.data, kelf.size, info, info_len, jkt, jkt_size};
  int rc = 0;
  rep->err = pfs_create_partition(j->visible_partition, CHANNEL_SIZE_STR, &rc);
  channel_result_t cr = {rep->err, rc, "create PFS partition"};
  if (!cr.err)
    cr = channel_populate(j->visible_partition, &c);
  if (!cr.err && advance(j, TX_CHANNEL_CREATED) == 0)
    cr = channel_verify(j->visible_partition, &c);
  if (!cr.err && advance(j, TX_CHANNEL_VERIFIED) == 0 &&
      advance(j, TX_COMPLETE) == 0) {
    rep->err = ERR_OK;
  } else {
    rep->err = cr.err ? cr.err : ERR_INTERNAL;
    rep->rc = cr.rc;
    rep->detail = cr.step;
    /* Never leave a visible channel that did not verify. The hidden
     * game stays so the channel can be repaired without recopying. */
    if (cr.err != ERR_PARTITION_EXISTS)
      hdd_remove_exact(j->visible_partition, NULL);
    tx_fail(j, rep->err);
    journal_save(j);
  }
  free(jkt_owned);
  payload_release(&kelf);
}

void game_install(game_plan_t *p, int allow_without_opl, const install_ui_t *ui,
                  install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  tx_journal_t j;
  memset(&j, 0, sizeof(j));
  stage(ui, rep, STAGE_PREPARING);

  /* 1. network ready */
  if (g_app.net != NETWORK_READY) {
    rep->err = ERR_NETWORK;
    goto out;
  }
  if (!g_app.app_mounted) {
    rep->err = ERR_JOURNAL;
    rep->detail = "installer partition PP.UDPFS-INSTALLER not mounted";
    goto out;
  }
  /* 2-4. source still accessible, re-parse and compare with the plan */
  source_udpfs_init(&g_src, &g_usrc);
  if ((rep->err = source_open(&g_src, p->source_path))) {
    rep->rc = g_src.last_rc;
    goto out;
  }
  iso_info_t again;
  if ((rep->err = iso_probe(&g_src, iso_hint_from_path(p->source_path), &again))) {
    rep->rc = g_src.last_rc;
    goto out_src;
  }
  if (strcmp(again.boot_id, p->iso.boot_id) || again.sectors != p->iso.sectors) {
    rep->err = ERR_SOURCE_INVALID_ISO;
    rep->detail = "source changed since it was selected";
    goto out_src;
  }
  /* 5. names are p->visible/p->hidden (one function, cannot drift) */
  if (!partition_pair_matches(p->visible, p->hidden)) {
    rep->err = ERR_INTERNAL;
    goto out_src;
  }
  /* 6. existing partitions: callers resolve these via pair actions */
  if (hdd_exists(p->hidden) != 0 || hdd_exists(p->visible) != 0) {
    rep->err = ERR_PARTITION_EXISTS;
    goto out_src;
  }
  /* 7. free space: data partitions + the 128 MiB channel */
  uint32_t total_mb, free_mb, max_mb;
  if (hdd_space_mb(&total_mb, &free_mb, &max_mb) < 0 ||
      (uint64_t)p->alloc.total_mb + CHANNEL_SIZE_MB > free_mb ||
      p->alloc.main_mb > max_mb) {
    rep->err = ERR_NO_SPACE;
    goto out_src;
  }
  /* 8. OPL runtime + launcher payload, before any HDD write */
  opl_runtime_t opl;
  int opl_ok = opl_check_runtime(&opl, &rep->rc) == ERR_OK;
  if (!opl_ok && !allow_without_opl) {
    rep->err = ERR_OPL_NOT_FOUND;
    goto out_src;
  }
  payload_t probe_kelf;
  if ((rep->err = payload_opl_launcher(&probe_kelf, g_app.app_mounted, 1))) {
    rep->detail = "put opl-launcher-EXECUTE.KELF in udpfs:/PAYLOAD/";
    goto out_src;
  }
  payload_release(&probe_kelf);

  /* 9. journal */
  str_copy(j.source_path, p->source_path, sizeof(j.source_path));
  j.source_size = p->iso.source_size;
  str_copy(j.startup_id, p->iso.boot_id, sizeof(j.startup_id));
  str_copy(j.visible_partition, p->visible, sizeof(j.visible_partition));
  str_copy(j.hidden_partition, p->hidden, sizeof(j.hidden_partition));
  j.bytes_expected = (uint64_t)p->iso.sectors * ISO_SECTOR;
  if (advance(&j, TX_PLANNED) < 0 || tx_save(APP_STATE_DIR, &j) != ERR_OK) {
    rep->err = ERR_JOURNAL;
    goto out_src;
  }

  /* 10-12. create + format hidden HDL partition */
  stage(ui, rep, STAGE_CREATING_HDL);
  struct HDLFS_FormatArgs args;
  hdl_format_args_build(&args, &p->iso, p->title);
  hdl_result_t hr = hdl_create_and_format(p->hidden, &p->alloc, &args);
  if (hr.err) {
    rep->err = hr.err;
    rep->rc = hr.rc;
    goto fail;
  }
  advance(&j, TX_HDL_CREATED);

  /* 13-15. stream */
  advance(&j, TX_STREAMING);
  stage(ui, rep, STAGE_COPYING);
  stream_cb_t cb = {ui ? ui->progress : NULL, ui ? ui->should_abort : NULL,
                    ui ? ui->ctx : NULL};
  hr = hdl_stream(p->hidden, &g_src, j.bytes_expected, &cb);
  j.bytes_written = hr.written;
  if (hr.err) {
    rep->err = hr.err;
    rep->rc = hr.rc;
    goto fail;
  }
  /* 16 */
  advance(&j, TX_HDL_COMPLETE);

  /* 17-18. verify */
  stage(ui, rep, STAGE_VALIDATING);
  hr = hdl_verify(p->hidden, &g_src, &p->iso, 1 + p->alloc.subs);
  if (hr.err) {
    rep->err = hr.err;
    rep->rc = hr.rc;
    goto fail;
  }
  advance(&j, TX_HDL_VERIFIED);
  source_close(&g_src);

  /* The channel is only created against a present OPL runtime. */
  if (!opl_ok && opl_check_runtime(&opl, &rep->rc) != ERR_OK) {
    rep->err = ERR_OPL_NOT_FOUND;
    rep->data_installed_no_channel = 1;
    goto out;
  }
  /* 19-31 */
  build_channel(&j, p->title, ui, rep);
  if (!rep->err)
    stage(ui, rep, STAGE_FINISHED);
  goto out;

fail:
  tx_fail(&j, rep->err);
  journal_save(&j);
out_src:
  source_close(&g_src);
out:
  finish_report(rep, p->visible, p->hidden);
}

void game_create_channel(const char *hidden, const install_ui_t *ui,
                         install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  char visible[APA_NAME_MAX + 1];
  stage(ui, rep, STAGE_VALIDATING);
  if (partition_partner(hidden, visible) < 0 || !partition_is_hidden_game(hidden)) {
    rep->err = ERR_INVALID_ARG;
    return;
  }
  if (!g_app.app_mounted) {
    rep->err = ERR_JOURNAL;
    rep->detail = "installer partition PP.UDPFS-INSTALLER not mounted";
    goto out;
  }
  pair_facts_t f;
  game_pair_facts(visible, hidden, &f);
  if (!pair_hidden_trusted(&f)) {
    rep->err = ERR_HDL_VERIFY;
    rep->detail = "hidden game data is not verified; reinstall instead";
    goto out;
  }
  hdl_header_info_t h;
  hdl_read_header(hidden, &h);

  opl_runtime_t opl;
  if ((rep->err = opl_check_runtime(&opl, &rep->rc)))
    goto out;

  tx_journal_t j;
  if (!load_pair_journal(hidden, &j)) {
    memset(&j, 0, sizeof(j));
    str_copy(j.startup_id, h.startup, sizeof(j.startup_id));
    str_copy(j.visible_partition, visible, sizeof(j.visible_partition));
    str_copy(j.hidden_partition, hidden, sizeof(j.hidden_partition));
    j.bytes_expected = j.bytes_written = h.data_bytes;
  }
  if (tx_advance(&j, TX_HDL_VERIFIED) != ERR_OK) {
    rep->err = ERR_JOURNAL;
    rep->detail = "journal state does not allow channel repair";
    goto out;
  }
  journal_save(&j);

  /* Rebuild: an existing (broken) PP. is removed first. */
  if (f.visible_exists) {
    if ((rep->err = hdd_remove_exact(visible, &rep->rc))) {
      rep->detail = visible;
      goto out;
    }
  }
  build_channel(&j, h.title, ui, rep);
  if (!rep->err)
    stage(ui, rep, STAGE_FINISHED);
out:
  finish_report(rep, visible, hidden);
}

inst_err_t game_delete_pair(const char *visible, const char *hidden,
                            const char **failed_name, int *rc_out) {
  *failed_name = NULL;
  *rc_out = 0;
  /* Visible first: a leftover hidden game is invisible to the XMB,
   * which is preferable to a visible broken channel. */
  if (visible && visible[0]) {
    if (hdd_remove_exact(visible, rc_out) != ERR_OK) {
      *failed_name = visible;
      return ERR_PARTITION_DELETE;
    }
  }
  if (hidden && hidden[0]) {
    if (hdd_remove_exact(hidden, rc_out) != ERR_OK) {
      *failed_name = hidden;
      return ERR_PARTITION_DELETE;
    }
  }
  char id[16];
  const char *any = (hidden && hidden[0]) ? hidden : visible;
  if (g_app.app_mounted && any && part_id_from_partition(any, id) == 0) {
    tx_journal_t j;
    char other[APA_NAME_MAX + 1] = "";
    if (any == visible)
      partition_partner(visible, other);
    const char *h = any == visible ? other : hidden;
    /* Only remove a journal that belongs to this pair. */
    if (tx_load(APP_STATE_DIR, id, &j) == ERR_OK && strcmp(j.hidden_partition, h) == 0)
      tx_remove(APP_STATE_DIR, id);
  }
  return ERR_OK;
}
