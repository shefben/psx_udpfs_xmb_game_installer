#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "hdd_partitions.h"
#include "hdl_header.h"
#include "hdl_install.h"
#include "manifest.h"
#include "opl_dependency.h"
#include "opl_launcher_payload.h"
#include "pfs_channel.h"
#include "server_assets.h"
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
    return "validating (full read-back)";
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
  /* udpfsd's prepared title (CFG / game list), for the same game only */
  const manifest_entry_t *me = g_manifest_loaded ? manifest_find_path(&g_manifest, path) : NULL;
  if (me && me->ok && !strcmp(me->id, p->iso.boot_id) && me->title[0])
    str_copy(title, me->title, sizeof(title));
  if (game_plan_set_title(p, title))
    return ERR_SOURCE_SYSTEM_CNF;
  /* Plan only with buckets this drive's APA driver accepts. */
  uint32_t max_mb = 0;
  if (hdd_space_mb(NULL, NULL, &max_mb) < 0)
    return ERR_HDD_MISSING;
  return hdl_plan_alloc((uint64_t)p->iso.sectors * ISO_SECTOR, max_mb, &p->alloc);
}

inst_err_t game_plan_from_manifest(const manifest_entry_t *m, game_plan_t *p) {
  memset(p, 0, sizeof(*p));
  if (!m->ok || m->bytes == 0 || m->bytes % ISO_SECTOR || m->bytes / ISO_SECTOR > 0xFFFFFFFFull)
    return ERR_SOURCE_INVALID_ISO;
  snprintf(p->source_path, sizeof(p->source_path), "udpfs:%.*s",
           (int)sizeof(p->source_path) - 7, m->path);
  p->type = source_classify(p->source_path);
  str_copy(p->iso.boot_id, m->id, sizeof(p->iso.boot_id));
  if (boot_id_to_part_id(m->id, p->iso.part_id))
    return ERR_SOURCE_SYSTEM_CNF;
  p->iso.source_size = m->bytes;
  p->iso.sectors = (uint32_t)(m->bytes / ISO_SECTOR);
  p->iso.disc_type = m->dvd ? DISC_TYPE_DVD : DISC_TYPE_CD;
  p->iso.layer1_start = m->layer1;
  if (game_plan_set_title(p, m->title))
    return ERR_INVALID_ARG;
  uint32_t max_mb = 0;
  if (hdd_space_mb(NULL, NULL, &max_mb) < 0)
    return ERR_HDD_MISSING;
  return hdl_plan_alloc(m->bytes, max_mb, &p->alloc);
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
  if (!g_app.app_mounted || tx_load(APP_STATE_DIR, hidden, j) != ERR_OK)
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
    f->journal_verified = tx_hidden_data_verified(&j);
    uint32_t start, size, hcrc;
    f->journal_matches_partition =
        f->hidden_exists && hdl_partition_identity(hidden, &start, &size, &hcrc) == 0 &&
        tx_identity_matches(&j, start, size, hcrc);
  }
  /* Only a PFS partition is an XMB channel; a same-named HDL partition
   * (hdl-dump visible install) still blocks a new install via
   * hdd_exists() in game_install step 6. */
  uint16_t vtype = 0;
  f->visible_exists = hdd_exists(visible) > 0 &&
                      hdd_stat(visible, &vtype, NULL, NULL) == 0 &&
                      partition_is_xmb_channel(visible, vtype);
  if (f->visible_exists)
    f->visible_valid = channel_quick_check(visible) == ERR_OK;
}

static int count_journals(void) {
  int dd = fileXioDopen(APP_STATE_DIR), n = 0;
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  while (fileXioDread(dd, &de) > 0)
    n += !strncmp(de.name, "install-", 8) && str_ends_with_ci(de.name, ".ini");
  fileXioDclose(dd);
  return n;
}

size_t game_pair_details(const char *visible, const char *hidden, char *out, size_t outsz) {
  pair_facts_t f;
  game_pair_facts(visible, hidden, &f);
  const char *why = pair_untrusted_reason(&f);
  uint16_t htype = 0, vtype = 0;
  uint32_t start = 0, size = 0, hcrc = 0;
  int hst = f.hidden_exists ? hdd_stat(hidden, &htype, NULL, NULL) : -2;
  int hid = f.hidden_exists ? hdl_partition_identity(hidden, &start, &size, &hcrc) : -2;
  int vst = hdd_exists(visible) > 0 ? hdd_stat(visible, &vtype, NULL, NULL) : -2;
  char fn[96] = "-";
  tx_journal_filename_for(hidden, fn);
  tx_journal_t j;
  memset(&j, 0, sizeof(j));
  inst_err_t je = g_app.app_mounted ? tx_load(APP_STATE_DIR, hidden, &j) : ERR_JOURNAL;

  int off = snprintf(out, outsz,
                     "State   %s\n"
                     "Reason  %s\n\n"
                     "Game data %s\n"
                     "  exists %s  stat %d  type 0x%04x  header %s\n"
                     "  identity rc %d  start %lu  size %lu  header CRC %08lx\n\n"
                     "Journal %s/%s\n"
                     "  installer partition %s  mounted %s  journals in folder %d\n"
                     "  (rename rc %d)\n",
                     pair_state_label(pair_classify(&f)), why ? why : "-", hidden,
                     f.hidden_exists ? "yes" : "NO", hst, htype,
                     f.hidden_header_valid ? "valid" : "INVALID", hid, (unsigned long)start,
                     (unsigned long)size, (unsigned long)hcrc, APP_STATE_DIR, fn,
                     g_app.app_rename_rc < 0 ? INSTALLER_LEGACY_NAME : INSTALLER_PARTITION,
                     g_app.app_mounted ? "yes" : "NO", g_app.app_mounted ? count_journals() : -1,
                     g_app.app_rename_rc);
  if (je == ERR_OK && off > 0 && (size_t)off < outsz)
    off += snprintf(
        out + off, outsz - off,
        "  state %s  failed_from %s  deleting %d\n"
        "  bytes expected %llu  written %llu  verified %llu\n"
        "  CRC source %s%08lx  installed %s%08lx\n"
        "  identity start %lu  size %lu  header CRC %08lx  (%s)\n"
        "  last error %s\n\n",
        tx_state_name(j.state), tx_state_name(j.failed_from), j.deleting,
        (unsigned long long)j.bytes_expected, (unsigned long long)j.bytes_written,
        (unsigned long long)j.bytes_verified, j.has_source_crc ? "" : "none ",
        (unsigned long)j.source_crc32, j.has_installed_crc ? "" : "none ",
        (unsigned long)j.installed_crc32, (unsigned long)j.hdl_start,
        (unsigned long)j.hdl_size, (unsigned long)j.hdl_header_crc32,
        !j.has_hdl_identity            ? "not recorded"
        : f.journal_matches_partition ? "matches"
                                      : "DIFFERS",
        j.last_error[0] ? j.last_error : "-");
  else if (off > 0 && (size_t)off < outsz)
    off += snprintf(out + off, outsz - off, "  NOT LOADED (missing or unreadable)\n\n");
  if (off > 0 && (size_t)off < outsz)
    off += snprintf(out + off, outsz - off,
                    "Channel %s\n"
                    "  exists %s  stat %d  type 0x%04x  files/header %s\n",
                    visible, f.visible_exists ? "yes" : "NO", vst, vtype,
                    !f.visible_exists ? "-" : f.visible_valid ? "valid" : "INVALID");
  if (off < 0)
    return 0;
  return (size_t)off < outsz ? (size_t)off : outsz - 1;
}

/* Jacket: udpfsd's prepared jkt/<BOOT_ID>.png, then udpfs:/ART/<BOOT_ID>.png,
 * then <source>.png, then the built-in default. Returns where it came
 * from (install_report_t.jacket). */
static const char *load_jacket(const char *boot_id, const char *source_path,
                               const uint8_t **data, uint32_t *size, void **owned) {
  char path[SOURCE_PATH_MAX + 8];
  const char *fallback = "default";
  *owned = NULL;
  if (g_app.net == NETWORK_READY) {
    const manifest_entry_t *me = g_manifest_loaded ? manifest_find_id(&g_manifest, boot_id) : NULL;
    if (me && me->jacket[0]) {
      void *buf = NULL;
      snprintf(path, sizeof(path), MANIFEST_DIR "/%s", me->jacket);
      int n = file_load(path, &buf, JACKET_MAX);
      if (n > 0 && png_basic_valid(buf, (uint32_t)n)) {
        *data = buf;
        *size = (uint32_t)n;
        *owned = buf;
        return "server";
      }
      free(buf);
      fallback = "missing";
    }
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
        return "server";
      }
      free(buf);
    }
  }
  payload_default_jacket(data, size);
  return fallback;
}

/* Journal writes are part of the transaction: a destructive step never
 * runs unless the state before it is on disk. */
static inst_err_t persist(const tx_journal_t *j) {
  if (!g_app.app_mounted)
    return ERR_JOURNAL;
  return tx_save(APP_STATE_DIR, j);
}

static inst_err_t advance(tx_journal_t *j, tx_state_t s) {
  if (tx_advance(j, s) != ERR_OK)
    return ERR_INTERNAL;
  return persist(j);
}

static void fail(tx_journal_t *j, install_report_t *rep, inst_err_t e, int rc,
                 const char *detail) {
  rep->err = e;
  rep->rc = rc;
  if (detail)
    rep->detail = detail;
  tx_fail(j, e);
  persist(j); /* best effort: the data is untrusted either way */
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

/* Copy udpfsd's CFG/<ID>.cfg (OPL per-game settings) to the OPL
 * partition's CFG folder, where OPL reads it when OPL-Launcher boots the
 * game - only if OPL has none yet. Best effort: written as .tmp, read
 * back, renamed. Returns "copied" | "kept" | "failed" | "none". */
static const char *copy_opl_cfg(const char *boot_id) {
  const manifest_entry_t *me = g_manifest_loaded ? manifest_find_id(&g_manifest, boot_id) : NULL;
  if (g_app.net != NETWORK_READY || !me || !me->cfg[0])
    return "none";
  opl_runtime_t opl;
  int rc;
  if (opl_check_runtime(&opl, &rc) != ERR_OK)
    return "failed";
  char src[SOURCE_PATH_MAX];
  snprintf(src, sizeof(src), "udpfs:%s", me->cfg);
  void *data = NULL;
  int n = file_load(src, &data, 64 * 1024);
  if (n <= 0) {
    free(data);
    return "failed";
  }
  const char *result = "failed";
  if (pfs_mount(PFS_WORK, opl.partition, FIO_MT_RDWR) == 0) {
    const char *dir = opl_cfg_dir(opl.partition); /* "pfs1:CFG/" or "pfs1:OPL/CFG/" */
    char dst[64], tmp[72], d[32];
    snprintf(dst, sizeof(dst), "%s%s.cfg", dir, boot_id);
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    switch (opl_cfg_decide(1, file_size(dst) >= 0)) {
    case OPL_CFG_KEEP:
      result = "kept";
      break;
    case OPL_CFG_COPY: {
      if (opl.partition[0] != '+')
        fileXioMkdir(PFS_WORK "OPL", 0777);
      snprintf(d, sizeof(d), "%.*s", (int)strlen(dir) - 1, dir);
      fileXioMkdir(d, 0777);
      void *back = NULL;
      fileXioRemove(tmp);
      if (file_write_all(tmp, data, (uint32_t)n) == 0 &&
          file_load(tmp, &back, (uint32_t)n + 1) == n && !memcmp(back, data, (size_t)n) &&
          fileXioRename(tmp, dst) >= 0)
        result = "copied";
      else
        fileXioRemove(tmp);
      free(back);
      break;
    }
    case OPL_CFG_NONE:
      result = "none";
      break;
    }
    pfs_umount(PFS_WORK);
  }
  free(data);
  return result;
}

/* Build the channel for a verified hidden game. The journal is at
 * TX_HDL_VERIFIED and saved; `kelf` was loaded by the caller before
 * anything was removed, and is released here. */
static void build_channel(tx_journal_t *j, const char *title, payload_t *kelf,
                          const install_ui_t *ui, install_report_t *rep) {
  str_copy(j->launcher_source, !strcmp(kelf->origin, "server") ? "server" : "embedded",
           sizeof(j->launcher_source));
  stage(ui, rep, STAGE_CREATING_CHANNEL);
  char info[1024];
  uint32_t info_len = (uint32_t)xmb_game_info_sys(info, sizeof(info), title,
                                                  j->startup_id);
  const uint8_t *jkt;
  uint32_t jkt_size;
  void *jkt_owned;
  rep->jacket = load_jacket(j->startup_id, j->source_path, &jkt, &jkt_size, &jkt_owned);

  channel_content_t c = {kelf->data, kelf->size, info, info_len, jkt, jkt_size};
  int rc = 0;
  inst_err_t e = pfs_create_partition(j->visible_partition, CHANNEL_SIZE_STR, &rc);
  channel_result_t cr = {e, rc, "create PFS partition"};
  if (!cr.err)
    cr = channel_populate(j->visible_partition, &c);
  if (!cr.err && (e = advance(j, TX_CHANNEL_CREATED)))
    cr = (channel_result_t){e, 0, "journal"};
  if (!cr.err)
    cr = channel_verify(j->visible_partition, &c);
  if (!cr.err && (e = advance(j, TX_CHANNEL_VERIFIED)))
    cr = (channel_result_t){e, 0, "journal"};
  if (!cr.err && (e = advance(j, TX_COMPLETE)))
    cr = (channel_result_t){e, 0, "journal"};

  if (cr.err) {
    /* Never leave a visible channel that did not verify. The hidden
     * game stays so the channel can be repaired without recopying. A
     * failed step may have left the work mount open, which makes the
     * APA driver refuse removal (-EBUSY), so release it first. */
    fail(j, rep, cr.err, cr.rc, cr.step);
    if (cr.err != ERR_PARTITION_EXISTS) {
      pfs_umount(PFS_WORK);
      int rrc = 0;
      if (hdd_remove_exact(j->visible_partition, &rrc) != ERR_OK) {
        rep->detail = "unverified channel could NOT be removed: use Repair";
        rep->rc = rrc;
      }
    }
  } else {
    rep->err = ERR_OK;
    /* The game is complete; its OPL settings are a best-effort extra. */
    rep->opl_cfg = copy_opl_cfg(j->startup_id);
    str_copy(j->opl_cfg, rep->opl_cfg, sizeof(j->opl_cfg));
    persist(j);
  }
  free(jkt_owned);
  payload_release(kelf);
}

void game_install(game_plan_t *p, int allow_without_opl, const install_ui_t *ui,
                  install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  tx_journal_t j;
  memset(&j, 0, sizeof(j));
  payload_t kelf;
  memset(&kelf, 0, sizeof(kelf));
  stage(ui, rep, STAGE_PREPARING);

  /* 1. network ready; journal storage present */
  if (g_app.net != NETWORK_READY) {
    rep->err = ERR_NETWORK;
    goto out;
  }
  if (!g_app.app_mounted) {
    rep->err = ERR_JOURNAL;
    rep->detail = "installer partition " INSTALLER_PARTITION " not mounted";
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
  /* The PS2's own probe is authoritative for everything the HDL header
   * and the verification use (volume ID, PVD size, disc type, layer
   * break); a plan from the server's manifest only chose the game. */
  p->iso = again;
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
      (uint64_t)p->alloc.total_mb + CHANNEL_SIZE_MB > free_mb) {
    rep->err = ERR_NO_SPACE;
    goto out_src;
  }
  hdl_alloc_t check;
  if (hdl_plan_alloc((uint64_t)p->iso.sectors * ISO_SECTOR, max_mb, &check) ||
      check.total_mb != p->alloc.total_mb) {
    rep->err = ERR_HDL_PLAN;
    goto out_src;
  }
  /* 8. OPL runtime + launcher payload, before any HDD write */
  opl_runtime_t opl;
  int opl_ok = opl_check_runtime(&opl, &rep->rc) == ERR_OK;
  if (!opl_ok && !allow_without_opl) {
    rep->err = ERR_OPL_NOT_FOUND;
    goto out_src;
  }
  if ((rep->err = payload_opl_launcher(&kelf, g_app.net == NETWORK_READY))) {
    rep->detail = "OPL-Launcher EXECUTE.KELF (not embedded in this build)";
    goto out_src;
  }

  /* 9. journal: TX_PLANNED on disk before the first HDD write */
  str_copy(j.source_path, p->source_path, sizeof(j.source_path));
  j.source_size = p->iso.source_size;
  str_copy(j.startup_id, p->iso.boot_id, sizeof(j.startup_id));
  str_copy(j.visible_partition, p->visible, sizeof(j.visible_partition));
  str_copy(j.hidden_partition, p->hidden, sizeof(j.hidden_partition));
  j.bytes_expected = (uint64_t)p->iso.sectors * ISO_SECTOR;
  if (advance(&j, TX_PLANNED) != ERR_OK) {
    rep->err = ERR_JOURNAL;
    goto out_src;
  }

  /* 10-12. create + format hidden HDL partition */
  stage(ui, rep, STAGE_CREATING_HDL);
  struct HDLFS_FormatArgs args;
  hdl_format_args_build(&args, &p->iso, p->title);
  hdl_result_t hr = hdl_create_and_format(p->hidden, &p->alloc, &args);
  if (hr.err) {
    fail(&j, rep, hr.err, hr.rc, NULL);
    goto out_src;
  }
  /* Bind the journal to this physical partition (start sector, size,
   * header CRC) before any data is written. */
  if (hdl_partition_identity(p->hidden, &j.hdl_start, &j.hdl_size,
                             &j.hdl_header_crc32) < 0) {
    fail(&j, rep, ERR_HDL_VERIFY, 0, "cannot read new partition identity");
    goto out_src;
  }
  j.has_hdl_identity = 1;
  if ((rep->err = advance(&j, TX_HDL_CREATED)) ||
      (rep->err = advance(&j, TX_STREAMING))) {
    fail(&j, rep, ERR_JOURNAL, 0, NULL);
    goto out_src;
  }

  /* 13-15. stream, CRC-32 over every byte received */
  stage(ui, rep, STAGE_COPYING);
  stream_cb_t cb = {ui ? ui->progress : NULL, ui ? ui->should_abort : NULL,
                    ui ? ui->ctx : NULL};
  hr = hdl_stream(p->hidden, &g_src, j.bytes_expected, &cb);
  j.bytes_written = hr.bytes;
  rep->bytes_written = hr.bytes;
  if (hr.err) {
    fail(&j, rep, hr.err, hr.rc, NULL);
    goto out_src;
  }
  source_close(&g_src); /* verification does not re-read the network */
  j.has_source_crc = 1;
  j.source_crc32 = hr.crc32;
  rep->source_crc32 = hr.crc32;
  /* 16 */
  if (advance(&j, TX_HDL_COMPLETE)) {
    fail(&j, rep, ERR_JOURNAL, 0, NULL);
    goto out;
  }

  /* 17-18. full read-back from the HDD and CRC comparison */
  stage(ui, rep, STAGE_VALIDATING);
  hr = hdl_verify(p->hidden, &p->iso, 1 + p->alloc.subs, &cb);
  j.bytes_verified = hr.bytes;
  rep->bytes_verified = hr.bytes;
  if (!hr.err) {
    j.has_installed_crc = 1;
    j.installed_crc32 = hr.crc32;
    rep->installed_crc32 = hr.crc32;
    rep->have_crc = 1;
  }
  if (hr.err) {
    fail(&j, rep, hr.err, hr.rc, "read-back of installed data");
    goto out;
  }
  if (hr.bytes != j.bytes_expected || hr.crc32 != j.source_crc32) {
    fail(&j, rep, ERR_HDL_VERIFY, 0, "CRC-32 of installed data != source stream");
    goto out;
  }
  if (advance(&j, TX_HDL_VERIFIED) || !tx_hidden_data_verified(&j)) {
    fail(&j, rep, ERR_JOURNAL, 0, NULL);
    goto out;
  }

  /* The channel is only created against a present OPL runtime. */
  if (!opl_ok && opl_check_runtime(&opl, &rep->rc) != ERR_OK) {
    rep->err = ERR_OPL_NOT_FOUND;
    rep->data_installed_no_channel = 1;
    goto out;
  }
  /* 19-31 */
  build_channel(&j, p->title, &kelf, ui, rep);
  if (!rep->err)
    stage(ui, rep, STAGE_FINISHED);
  goto out;

out_src:
  source_close(&g_src);
out:
  payload_release(&kelf);
  finish_report(rep, p->visible, p->hidden);
}

void game_create_channel(const char *hidden, const install_ui_t *ui,
                         install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  char visible[APA_NAME_MAX + 1];
  payload_t kelf;
  memset(&kelf, 0, sizeof(kelf));
  stage(ui, rep, STAGE_VALIDATING);
  if (partition_partner(hidden, visible) < 0 || !partition_is_hidden_game(hidden)) {
    rep->err = ERR_INVALID_ARG;
    return;
  }
  if (!g_app.app_mounted) {
    rep->err = ERR_JOURNAL;
    rep->detail = "installer partition " INSTALLER_PARTITION " not mounted";
    goto out;
  }
  pair_facts_t f;
  game_pair_facts(visible, hidden, &f);
  tx_journal_t j;
  if (!pair_hidden_trusted(&f) || !load_pair_journal(hidden, &j)) {
    rep->err = ERR_HDL_VERIFY;
    rep->detail = "no completed, CRC-verified install journal; reinstall instead";
    goto out;
  }
  hdl_header_info_t h;
  if (hdl_read_header(hidden, &h) < 0) {
    rep->err = ERR_HDL_VERIFY;
    goto out;
  }

  /* Everything the new channel needs is checked/loaded before an
   * existing channel is touched. */
  opl_runtime_t opl;
  if ((rep->err = opl_check_runtime(&opl, &rep->rc)))
    goto out;
  if ((rep->err = payload_opl_launcher(&kelf, g_app.net == NETWORK_READY))) {
    rep->detail = "OPL-Launcher EXECUTE.KELF (existing channel left untouched)";
    goto out;
  }
  if (tx_advance(&j, TX_HDL_VERIFIED) != ERR_OK || persist(&j) != ERR_OK) {
    rep->err = ERR_JOURNAL;
    goto out;
  }

  /* Rebuild: an existing (broken) PP. is removed first. */
  if (f.visible_exists && (rep->err = hdd_remove_exact(visible, &rep->rc))) {
    rep->detail = visible;
    goto out;
  }
  build_channel(&j, h.title, &kelf, ui, rep);
  if (!rep->err)
    stage(ui, rep, STAGE_FINISHED);
out:
  payload_release(&kelf);
  finish_report(rep, visible, hidden);
}

inst_err_t game_delete_pair(const char *visible, const char *hidden,
                            const char **failed_name, int *rc_out) {
  *failed_name = NULL;
  *rc_out = 0;
  const char *any = (hidden && hidden[0]) ? hidden : visible;

  /* Before the first removal: the data is no longer trusted, whatever
   * happens next (power loss between the two removals included). */
  tx_journal_t j;
  if (g_app.app_mounted && any && tx_load(APP_STATE_DIR, any, &j) == ERR_OK) {
    j.deleting = 1;
    if (persist(&j) != ERR_OK) {
      *failed_name = "journal";
      return ERR_JOURNAL;
    }
  }
  /* Visible first: a leftover hidden game is invisible to the XMB,
   * which is preferable to a visible broken channel. */
  if (visible && visible[0] && hdd_remove_exact(visible, rc_out) != ERR_OK) {
    *failed_name = visible;
    return ERR_PARTITION_DELETE;
  }
  if (hidden && hidden[0] && hdd_remove_exact(hidden, rc_out) != ERR_OK) {
    *failed_name = hidden;
    return ERR_PARTITION_DELETE;
  }
  /* The journal file is named after this exact pair; only remove it
   * once neither partition remains. */
  char partner[APA_NAME_MAX + 1];
  if (g_app.app_mounted && any && partition_partner(any, partner) == 0 &&
      hdd_exists(any) == 0 && hdd_exists(partner) == 0)
    tx_remove(APP_STATE_DIR, any);
  return ERR_OK;
}
