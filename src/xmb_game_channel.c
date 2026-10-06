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
#include "source_zso.h"
#include "transaction.h"
#include "util.h"
#include "xmb_game_channel.h"
#include "xmb_text.h"

#define JACKET_MAX (512 * 1024)

const char *install_stage_name(install_stage_t s) {
  switch (s) {
  case STAGE_PREPARING:
    return "preparing";
  case STAGE_CHECKING_RESUME:
    return "checking the data already copied";
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

int game_source_is_server(const char *path) { return !strncmp(path, "udpfs:", 6); }

static GameSource g_src, g_inner, g_fallback;
static udpfs_src_t g_usrc, g_ufb;
static zso_src_t g_zso;

/* Any fileXio device (udpfs:, mass0:). ZSO is decompressed here: a raw
 * .zso (USB), and udpfsd's virtual "<x>.zso.iso" too - the raw .zso next
 * to it is read instead, so only the compressed bytes cross the network.
 * Same logical ISO bytes either way (CRC, resume, verify unchanged). */
static void source_init_for(const char *path, int zso_on_ps2) {
  int virt = zso_on_ps2 && game_source_is_server(path) && source_classify(path) == SRC_TYPE_ZSO;
  if (source_is_raw_zso(path) || virt) {
    source_udpfs_init(&g_inner, &g_usrc);
    source_zso_init(&g_src, &g_zso, &g_inner);
    g_zso.strip_iso = virt;
    if (virt) { /* udpfsd's own decompression if a block fails here */
      source_udpfs_init(&g_fallback, &g_ufb);
      g_zso.fallback = &g_fallback;
    }
  } else {
    source_udpfs_init(&g_src, &g_usrc);
  }
}

/* Open with PS2-side ZSO decompression; if that fails (a ZSO variant this
 * reader does not support), fall back to udpfsd's decompression. */
static inst_err_t game_source_open(const char *path) {
  source_init_for(path, 1);
  inst_err_t e = source_open(&g_src, path);
  if (e && game_source_is_server(path) && source_classify(path) == SRC_TYPE_ZSO) {
    source_init_for(path, 0);
    e = source_open(&g_src, path);
  }
  return e;
}


inst_err_t game_plan_build(const char *path, game_plan_t *p, int *rc_out) {
  memset(p, 0, sizeof(*p));
  *rc_out = 0;
  str_copy(p->source_path, path, sizeof(p->source_path));
  p->type = source_classify(path);

  inst_err_t e = game_source_open(path);
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

int game_data_partition(const char *hidden, char out[APA_NAME_MAX + 1]) {
  uint16_t t = 0;
  if (hdd_stat(hidden, &t, NULL, NULL) == 0 && t == APA_TYPE_HDL_ID) {
    str_copy(out, hidden, APA_NAME_MAX + 1);
    return 0;
  }
  if (partition_partner(hidden, out) == 0 && hdd_stat(out, &t, NULL, NULL) == 0 &&
      t == APA_TYPE_HDL_ID)
    return 1;
  str_copy(out, hidden, APA_NAME_MAX + 1);
  return -1;
}

void game_pair_facts(const char *visible, const char *hidden, pair_facts_t *f) {
  memset(f, 0, sizeof(*f));
  hdl_header_info_t h;
  char data[APA_NAME_MAX + 1];
  int where = game_data_partition(hidden, data);
  f->hidden_exists = where >= 0; /* the HDL game partition, either name */
  f->data_visible = where == 1;
  if (f->hidden_exists)
    f->hidden_header_valid = hdl_partition_looks_valid(data, &h);
  tx_journal_t j;
  if (load_pair_journal(hidden, &j)) {
    f->has_journal = 1;
    f->journal_verified = tx_hidden_data_verified(&j);
    f->verify_skipped = j.verify_skipped;
    uint32_t start, size, hcrc;
    /* A rename (__. <-> PP.) keeps start, size and HDL header. */
    f->journal_matches_partition =
        f->hidden_exists && hdl_partition_identity(data, &start, &size, &hcrc) == 0 &&
        tx_identity_matches(&j, start, size, hcrc);
    f->resumable = tx_resumable(&j) && f->journal_matches_partition && !f->data_visible;
    f->resume_bytes = f->resumable ? j.bytes_written : 0;
  }
  /* In the XMB: the game partition itself under its PP. name with a boot
   * header (PFS-BatchKit-Manager's layout, ours since 2.0), or an older
   * release's PFS channel next to the hidden game, which "Rebuild XMB
   * channel" converts. */
  uint16_t vtype = 0;
  f->legacy_channel = !f->data_visible && hdd_exists(visible) > 0 &&
                      hdd_stat(visible, &vtype, NULL, NULL) == 0 &&
                      partition_is_xmb_channel(visible, vtype);
  f->visible_exists = f->data_visible || f->legacy_channel;
  /* Shown game: its boot header. Cover partition (PFS PP.X next to the
   * hidden game, PFS-BatchKit-Manager's "resource partition"): its files
   * and header. */
  f->visible_valid = f->data_visible ? game_header_check(data) == ERR_OK
                                     : f->legacy_channel && channel_quick_check(visible) == ERR_OK;
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

inst_err_t game_resume_plan(const char *hidden, game_plan_t *p, int *rc_out) {
  tx_journal_t j;
  *rc_out = 0;
  if (!load_pair_journal(hidden, &j) || !tx_resumable(&j))
    return ERR_JOURNAL;
  inst_err_t e = game_plan_build(j.source_path, p, rc_out);
  if (e)
    return e;
  /* The partitions keep the names they were created with. */
  str_copy(p->visible, j.visible_partition, sizeof(p->visible));
  str_copy(p->hidden, j.hidden_partition, sizeof(p->hidden));
  p->resume = 1;
  return ERR_OK;
}

size_t game_pair_details(const char *visible, const char *hidden, char *out, size_t outsz) {
  pair_facts_t f;
  game_pair_facts(visible, hidden, &f);
  const char *why = pair_untrusted_reason(&f);
  uint16_t htype = 0, vtype = 0;
  uint32_t start = 0, size = 0, hcrc = 0;
  char data[APA_NAME_MAX + 1];
  game_data_partition(hidden, data);
  int hst = f.hidden_exists ? hdd_stat(data, &htype, NULL, NULL) : -2;
  int hid = f.hidden_exists ? hdl_partition_identity(data, &start, &size, &hcrc) : -2;
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
                     pair_state_label(pair_classify(&f)), why ? why : "-", data,
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
                    "XMB %s\n"
                    "  %s  stat %d  type 0x%04x  boot header %s\n",
                    visible,
                    f.data_visible     ? "shown (game partition, PATINFO boot)"
                    : f.legacy_channel ? "OLD PFS channel: Rebuild converts it"
                                       : "not shown (hidden game)",
                    vst, vtype,
                    !f.data_visible ? "-" : f.visible_valid ? "valid" : "INVALID");
  if (off < 0)
    return 0;
  return (size_t)off < outsz ? (size_t)off : outsz - 1;
}


int game_load_info(const char *boot_id, xmb_game_info_t *gi) {
  if (g_app.net != NETWORK_READY || !boot_id_is_valid(boot_id))
    return 0;
  char path[64];
  snprintf(path, sizeof(path), MANIFEST_DIR "/info/%s.txt", boot_id);
  void *buf = NULL;
  int n = file_load(path, &buf, 2047); /* allocates exactly n bytes */
  int ok = 0;
  if (n > 0) {
    char s[2048];
    memcpy(s, buf, (size_t)n);
    s[n] = 0;
    ok = xmb_game_info_parse(s, gi) > 0;
  }
  free(buf);
  return ok;
}

const char *game_load_jackets(const char *boot_id, jacket_pair_t *j, void *owned[2]) {
  owned[0] = owned[1] = NULL;
  payload_default_jackets(j);
  const manifest_entry_t *me = g_app.net == NETWORK_READY && g_manifest_loaded
                                   ? manifest_find_id(&g_manifest, boot_id)
                                   : NULL;
  if (!me || !me->jacket[0])
    return "default";
  /* udpfsd: jkt/<ID>.png (74x108) and jkt/<ID>_L.png (140x200). */
  char small[SOURCE_PATH_MAX + 8], large[SOURCE_PATH_MAX + 16];
  snprintf(small, sizeof(small), MANIFEST_DIR "/%s", me->jacket);
  size_t n = strlen(small);
  if (n < 5 || strcmp(small + n - 4, ".png"))
    return "missing";
  snprintf(large, sizeof(large), "%.*s_L.png", (int)(n - 4), small);
  void *a = NULL, *b = NULL;
  int na = file_load(large, &a, JACKET_MAX), nb = file_load(small, &b, JACKET_MAX);
  if (na > 0 && nb > 0 && png_is_size(a, (uint32_t)na, JKT_LARGE_W, JKT_LARGE_H) &&
      png_is_size(b, (uint32_t)nb, JKT_SMALL_W, JKT_SMALL_H)) {
    *j = (jacket_pair_t){a, (uint32_t)na, b, (uint32_t)nb};
    owned[0] = a;
    owned[1] = b;
    return "server";
  }
  free(a);
  free(b);
  return "missing"; /* an older udpfsd (no large cover) or a bad file */
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

/* stream_cb_t.checkpoint: the first `bytes` are on the HDD; save where
 * a resumed copy would continue. */
static seg_list_t g_segs;

static int journal_checkpoint(void *cp_ctx, uint64_t bytes, uint32_t cum_crc, uint32_t seg_crc) {
  tx_journal_t *j = cp_ctx;
  /* Segment list first: a journal never names a checkpoint whose
   * segment cannot be re-checked. */
  if (seg_add(&g_segs, bytes, cum_crc, seg_crc) < 0 ||
      tx_seg_save(APP_STATE_DIR, j->hidden_partition, &g_segs) != ERR_OK)
    return 1;
  j->bytes_written = bytes;
  j->has_resume_crc = 1;
  j->resume_crc32 = cum_crc;
  return persist(j) != ERR_OK;
}

static const char *g_crc_hidden;
static int crc_hdd(void *ctx, uint64_t start, uint64_t end, uint32_t *crc) {
  (void)ctx;
  return hdl_crc_range(g_crc_hidden, start, end, crc);
}

static void stage(const install_ui_t *ui, install_report_t *rep, install_stage_t s) {
  rep->stage = s;
  if (ui && ui->stage)
    ui->stage(ui->ctx, s);
}

/* Resume: read back the newest checkpoint segments from the HDD and keep
 * the newest one that is still correct; everything after it is copied
 * again. Without a segment list (older journal) the journal's checkpoint
 * is used as it is. The full read-back after the copy checks the rest. */
static void pick_resume_point(tx_journal_t *j, const install_ui_t *ui, install_report_t *rep) {
  if (tx_seg_load(APP_STATE_DIR, j->hidden_partition, &g_segs) != ERR_OK || g_segs.n == 0)
    return;
  /* The list may run ahead of the journal (power cut between the two). */
  while (g_segs.n && g_segs.s[g_segs.n - 1].bytes > j->bytes_written)
    g_segs.n--;
  stage(ui, rep, STAGE_CHECKING_RESUME);
  g_crc_hidden = j->hidden_partition;
  int checked = 0;
  int k = seg_pick_resume(&g_segs, 4, crc_hdd, NULL, &checked);
  rep->resume_checked = checked;
  if (k < 0) { /* nothing re-checks: start the copy over (same partition) */
    g_segs.n = 0;
    j->bytes_written = 0;
    j->resume_crc32 = 0;
  } else {
    g_segs.n = k + 1;
    j->bytes_written = g_segs.s[k].bytes;
    j->resume_crc32 = g_segs.s[k].cum_crc;
  }
  tx_seg_save(APP_STATE_DIR, j->hidden_partition, &g_segs);
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

/* Show a verified game in the XMB the way PFS-BatchKit-Manager does (its
 * games load together on a DESR; separate PFS channels froze the XMB
 * once two existed): the game's own HDL partition gets the boot header
 * (system.cnf "BOOT2 = PATINFO", icon.sys, icon, OPL-Launcher as boot
 * KELF) and is renamed __.X -> PP.X. The header is written and checked
 * while the game is still hidden; an older release's PFS channel PP.X is
 * removed only then. The journal is at TX_HDL_VERIFIED and saved; `kelf`
 * was loaded by the caller and is released here. */
static void build_channel(tx_journal_t *j, const char *title, payload_t *kelf,
                          const install_ui_t *ui, install_report_t *rep) {
  str_copy(j->launcher_source, !strcmp(kelf->origin, "server") ? "server" : "embedded",
           sizeof(j->launcher_source));
  stage(ui, rep, STAGE_CREATING_CHANNEL);
  rep->jacket = NULL; /* the XMB shows the header's icon for these games */
  char data[APA_NAME_MAX + 1], part_id[PART_ID_LEN + 1] = "";
  boot_id_to_part_id(j->startup_id, part_id);
  int rc = 0, where = game_data_partition(j->hidden_partition, data);
  channel_result_t cr = {ERR_OK, 0, NULL};
  inst_err_t e;
  if (where < 0)
    cr = (channel_result_t){ERR_HDL_VERIFY, 0, "game partition missing"};
  if (!cr.err && (e = game_header_write(data, title, part_id, kelf->data, kelf->size, &rc)))
    cr = (channel_result_t){e, rc, "boot header (PATINFO, icon, OPL-Launcher)"};
  if (!cr.err && (e = advance(j, TX_CHANNEL_CREATED)))
    cr = (channel_result_t){e, 0, "journal"};
  if (!cr.err && where == 0) {
    uint16_t t = 0;
    if (hdd_exists(j->visible_partition) > 0) {
      /* Only an older release's PFS channel may stand in the way. */
      if (hdd_stat(j->visible_partition, &t, NULL, NULL) == 0 && t == APA_TYPE_PFS_ID) {
        pfs_umount(PFS_WORK);
        if ((e = hdd_remove_exact(j->visible_partition, &rc)))
          cr = (channel_result_t){e, rc, "remove the old PFS channel"};
      } else {
        cr = (channel_result_t){ERR_PARTITION_EXISTS, t, "a PP. partition of another kind"};
      }
    }
    fileXioUmount("hdl0:"); /* an open game partition cannot be renamed (-EBUSY) */
    if (!cr.err && (e = hdd_rename_game(data, j->visible_partition, &rc)))
      cr = (channel_result_t){e, rc, "show in the XMB (rename __. to PP.)"};
    if (!cr.err)
      str_copy(data, j->visible_partition, sizeof(data));
  }
  if (!cr.err && (e = game_header_verify(data, title, part_id, kelf->data, kelf->size, &rc)))
    cr = (channel_result_t){e, rc, "boot header read-back"};
  if (!cr.err && (e = advance(j, TX_CHANNEL_VERIFIED)))
    cr = (channel_result_t){e, 0, "journal"};
  if (!cr.err && (e = advance(j, TX_COMPLETE)))
    cr = (channel_result_t){e, 0, "journal"};

  if (cr.err) {
    /* Nothing is removed: the verified game stays (hidden: "channel
     * pending", shown: "channel BROKEN"), and Create/Repair XMB channel
     * finishes it without copying again. */
    fail(j, rep, cr.err, cr.rc, cr.step);
  } else {
    rep->err = ERR_OK;
    /* The game is complete; its OPL settings are a best-effort extra. */
    rep->opl_cfg = copy_opl_cfg(j->startup_id);
    str_copy(j->opl_cfg, rep->opl_cfg, sizeof(j->opl_cfg));
    persist(j);
  }
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

  /* 1. network ready (server images); journal storage present */
  if (game_source_is_server(p->source_path) && g_app.net != NETWORK_READY) {
    rep->err = ERR_NETWORK;
    goto out;
  }
  if (!g_app.app_mounted) {
    rep->err = ERR_JOURNAL;
    rep->detail = "installer partition " INSTALLER_PARTITION " not mounted";
    goto out;
  }
  /* 2-4. source still accessible, re-parse and compare with the plan */
  if ((rep->err = game_source_open(p->source_path))) {
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
  /* 6. existing partitions: callers resolve these via pair actions. A
   * resume needs exactly the interrupted copy: its journal, bound to
   * the same physical partition, for this same image. */
  if (p->resume) {
    uint32_t s = 0, z = 0, c = 0;
    if (!load_pair_journal(p->hidden, &j) || !tx_resumable(&j) ||
        strcmp(j.visible_partition, p->visible) || strcmp(j.startup_id, p->iso.boot_id) ||
        strcmp(j.source_path, p->source_path) || j.source_size != p->iso.source_size ||
        j.bytes_expected != (uint64_t)p->iso.sectors * ISO_SECTOR ||
        hdd_exists(p->visible) != 0 ||
        hdl_partition_identity(p->hidden, &s, &z, &c) < 0 || !tx_identity_matches(&j, s, z, c)) {
      rep->err = ERR_JOURNAL;
      rep->detail = "cannot resume: journal, partition or source changed; reinstall";
      goto out_src;
    }
  } else if (hdd_exists(p->hidden) != 0 || hdd_exists(p->visible) != 0) {
    rep->err = ERR_PARTITION_EXISTS;
    goto out_src;
  }
  /* 7. free space: the data partitions (a resume already holds them).
   * The XMB entry is the game partition itself: no extra partition. */
  uint32_t total_mb, free_mb, max_mb;
  if (hdd_space_mb(&total_mb, &free_mb, &max_mb) < 0) {
    rep->err = ERR_HDD_MISSING;
    goto out_src;
  }
  /* HDD free space and the 128 GiB limit for games and data. */
  if (!p->resume && (rep->err = hdd_space_check(p->alloc.total_mb)))
    goto out_src;
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

  hdl_result_t hr;
  if (p->resume) {
    pick_resume_point(&j, ui, rep);
    if (advance(&j, TX_STREAMING)) {
      rep->err = ERR_JOURNAL;
      goto out_src;
    }
    goto stream;
  }
  /* A fresh copy: no checkpoints from an earlier attempt. */
  memset(&g_segs, 0, sizeof(g_segs));
  tx_seg_remove(APP_STATE_DIR, p->hidden);
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
  hr = hdl_create_and_format(p->hidden, &p->alloc, &args);
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

  /* 13-15. stream, CRC-32 over every byte received; a checkpoint is
   * saved every STREAM_CHECKPOINT bytes so a cut copy can resume. */
stream:
  stage(ui, rep, STAGE_COPYING);
  stream_cb_t cb = {ui ? ui->progress : NULL, ui ? ui->should_abort : NULL,
                    ui ? ui->ctx : NULL, journal_checkpoint, &j};
  uint64_t from = p->resume ? j.bytes_written : 0;
  hr = hdl_stream(p->hidden, &g_src, j.bytes_expected, from, p->resume ? j.resume_crc32 : 0,
                  &cb);
  rep->bytes_written = hr.bytes;
  rep->resumed_from = from;
  if (!hr.err) {
    j.bytes_written = hr.bytes;
    j.has_resume_crc = 0;
    j.resume_crc32 = 0;
    tx_seg_remove(APP_STATE_DIR, p->hidden); /* copy finished */
  } else if (hr.err == ERR_USER_ABORT && ui && ui->paused && ui->paused(ui->ctx)) {
    /* Paused: the stop point is already saved as a checkpoint. */
    hr.err = ERR_PAUSED;
  }
  /* On failure bytes_written stays at the last checkpoint (resume point). */
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
  if (hr.err == ERR_USER_ABORT && !p->resume && ui && ui->skip_verify &&
      ui->skip_verify(ui->ctx)) {
    /* Skipped by the user: the HDL header and the PVD (first block) were
     * checked, the full CRC read-back was not. The copy itself is
     * complete with its source CRC; "Verify game data" can finish it. */
    j.verify_skipped = 1;
    rep->verify_skipped = 1;
  } else {
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

void game_verify_data(const char *hidden, const install_ui_t *ui, install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  stage(ui, rep, STAGE_VALIDATING);
  tx_journal_t j;
  pair_facts_t f;
  char visible[APA_NAME_MAX + 1];
  if (partition_partner(hidden, visible) < 0 || !partition_is_hidden_game(hidden)) {
    rep->err = ERR_INVALID_ARG;
    return;
  }
  game_pair_facts(visible, hidden, &f);
  /* Only a copy this installer completed has a source CRC to compare. */
  if (!load_pair_journal(hidden, &j) || j.deleting || !j.has_source_crc ||
      j.bytes_expected == 0 || j.bytes_written != j.bytes_expected ||
      !f.journal_matches_partition) {
    rep->err = ERR_HDL_VERIFY;
    rep->detail = "no completed install journal for this partition; reinstall instead";
    goto out;
  }
  stream_cb_t cb = {ui ? ui->progress : NULL, ui ? ui->should_abort : NULL,
                    ui ? ui->ctx : NULL, NULL, NULL};
  char data[APA_NAME_MAX + 1];
  game_data_partition(hidden, data); /* __.X, or PP.X once shown */
  hdl_result_t hr = hdl_read_back(data, j.bytes_expected, &cb);
  rep->bytes_written = j.bytes_written;
  rep->bytes_verified = hr.bytes;
  rep->source_crc32 = j.source_crc32;
  if (hr.err) {
    /* Aborted or unreadable: the journal is left as it was. */
    rep->err = hr.err;
    rep->rc = hr.rc;
    rep->detail = "read-back of installed data";
    goto out;
  }
  rep->have_crc = 1;
  rep->installed_crc32 = hr.crc32;
  j.bytes_verified = hr.bytes;
  j.has_installed_crc = 1;
  j.installed_crc32 = hr.crc32;
  j.verify_skipped = 0; /* the read-back ran: its result decides from now on */
  if (hr.crc32 != j.source_crc32) {
    str_copy(j.last_error, "ERR_HDL_VERIFY", sizeof(j.last_error));
    rep->err = ERR_HDL_VERIFY;
    rep->detail = "CRC-32 of installed data != source stream";
  }
  if (persist(&j) != ERR_OK && !rep->err)
    rep->err = ERR_JOURNAL;
out:
  finish_report(rep, visible, hidden);
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
  char data[APA_NAME_MAX + 1];
  game_data_partition(hidden, data);
  if (hdl_read_header(data, &h) < 0) {
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

  /* A title the user set with Rename is kept: from the boot header of a
   * shown game, or the info.sys of an older release's PFS channel (which
   * build_channel replaces). */
  char title[64] = "";
  if (f.data_visible)
    game_header_get_title(data, title, sizeof(title));
  else if (f.legacy_channel)
    channel_get_title(visible, title, sizeof(title));
  if (!title[0])
    str_copy(title, h.title, sizeof(title));
  build_channel(&j, title, &kelf, ui, rep);
  if (!rep->err)
    stage(ui, rep, STAGE_FINISHED);
out:
  payload_release(&kelf);
  finish_report(rep, visible, hidden);
}

inst_err_t game_remove_channel(const char *visible, int *rc_out) {
  *rc_out = 0;
  if (!partition_is_game_channel(visible))
    return ERR_INVALID_ARG;
  uint16_t t = 0;
  if (hdd_stat(visible, &t, NULL, NULL) == 0 && t == APA_TYPE_HDL_ID) {
    /* The game itself is shown: hide it again (PP.X -> __.X). */
    char hidden[APA_NAME_MAX + 1];
    partition_partner(visible, hidden);
    fileXioUmount("hdl0:");
    return hdd_rename_game(visible, hidden, rc_out);
  }
  pfs_umount(PFS_WORK); /* a mounted channel cannot be removed (-EBUSY) */
  return hdd_remove_exact(visible, rc_out);
}

/* Covers need a PFS partition with res/ (the XMB reads jkt_001/002.png
 * there; an HDL partition has none). PFS-BatchKit-Manager's "resource
 * partition": the game is hidden (PP.X -> __.X, its PATINFO header stays)
 * and a 128 MiB PFS PP.X gets EXECUTE.KELF (OPL-Launcher, which finds
 * the game under __.X), res/ and a pfs:/EXECUTE.KELF boot header. Any
 * failure removes the new partition and shows the game again. */
void game_add_cover(const char *hidden, const install_ui_t *ui, install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  char visible[APA_NAME_MAX + 1], part_id[PART_ID_LEN + 1] = "";
  payload_t kelf;
  memset(&kelf, 0, sizeof(kelf));
  jacket_pair_t jkt;
  void *jkt_owned[2] = {NULL, NULL};
  stage(ui, rep, STAGE_CREATING_CHANNEL);
  pair_facts_t f;
  if (partition_partner(hidden, visible) < 0 || !partition_is_hidden_game(hidden)) {
    rep->err = ERR_INVALID_ARG;
    return;
  }
  game_pair_facts(visible, hidden, &f);
  if (!pair_can_add_cover(&f)) {
    rep->err = ERR_INVALID_ARG;
    rep->detail = "only for an installed game shown in the XMB";
    goto out;
  }
  hdl_header_info_t h;
  char title[64] = "";
  if (hdl_read_header(visible, &h) < 0) {
    rep->err = ERR_HDL_VERIFY;
    goto out;
  }
  if (game_header_get_title(visible, title, sizeof(title)) < 0 || !title[0])
    str_copy(title, h.title, sizeof(title));
  boot_id_to_part_id(h.startup, part_id);
  if ((rep->err = payload_opl_launcher(&kelf, g_app.net == NETWORK_READY))) {
    rep->detail = "OPL-Launcher EXECUTE.KELF";
    goto out;
  }
  if ((rep->err = hdd_space_check(CHANNEL_SIZE_MB))) /* incl. the 128 GiB limit */
    goto out;
  char info[1024], today[9];
  xmb_game_info_t gi;
  int have_gi = game_load_info(h.startup, &gi);
  install_date(today);
  uint32_t info_len = (uint32_t)xmb_game_info_sys_ex(info, sizeof(info), title, h.startup,
                                                     have_gi ? &gi : NULL, today);
  rep->jacket = game_load_jackets(h.startup, &jkt, jkt_owned);
  channel_content_t c = {kelf.data, kelf.size, info, info_len, jkt, title, part_id};

  fileXioUmount("hdl0:");
  if ((rep->err = hdd_rename_game(visible, hidden, &rep->rc))) {
    rep->detail = "hide the game partition (PP. to __.)";
    goto out;
  }
  channel_result_t cr;
  int rc = 0;
  inst_err_t e = pfs_create_partition(visible, CHANNEL_SIZE_STR, &rc);
  cr = (channel_result_t){e, rc, "create the PFS cover partition"};
  if (!cr.err)
    cr = channel_populate(visible, &c);
  if (!cr.err)
    cr = channel_verify(visible, &c);
  if (cr.err) {
    rep->err = cr.err;
    rep->rc = cr.rc;
    rep->detail = cr.step;
    pfs_umount(PFS_WORK);
    int rrc = 0;
    if (hdd_remove_exact(visible, &rrc) != ERR_OK ||
        hdd_rename_game(hidden, visible, &rrc) != ERR_OK)
      rep->detail = "cover partition failed and the game could NOT be shown again: "
                    "use Rebuild XMB channel";
    goto out;
  }
  rep->err = ERR_OK;
  stage(ui, rep, STAGE_FINISHED);
out:
  free(jkt_owned[0]);
  free(jkt_owned[1]);
  payload_release(&kelf);
  finish_report(rep, visible, hidden);
}

inst_err_t game_set_title(const char *hidden, const char *title, int *rc_out) {
  char data[APA_NAME_MAX + 1], clean[64], part_id[PART_ID_LEN + 1];
  *rc_out = 0;
  if (game_data_partition(hidden, data) != 1)
    return ERR_INVALID_ARG; /* only a shown game has a title in the XMB */
  xmb_sanitize_value(title, clean, sizeof(clean));
  if (!clean[0] || strlen(hidden) < 3 + PART_ID_LEN)
    return ERR_INVALID_ARG;
  memcpy(part_id, hidden + 3, PART_ID_LEN);
  part_id[PART_ID_LEN] = 0;
  /* icon.sys only; the boot KELF stays as it is. */
  return game_header_write(data, clean, part_id, NULL, 0, rc_out);
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
