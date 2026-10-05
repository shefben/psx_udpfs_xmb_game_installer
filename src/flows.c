#include <kernel.h>
#include <libpwroff.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>

#include "app_state.h"
#include "backup.h"
#include "batch.h"
#include "browser.h"
#include "flows.h"
#include "hdd_partitions.h"
#include "hdl_install.h"
#include "manifest.h"
#include "network.h"
#include "opl_dependency.h"
#include "listui.h"
#include "opl_launcher_payload.h"
#include "partname.h"
#include "pfs_channel.h"
#include "pops_install.h"
#include "remove_games.h"
#include "settings.h"
#include "transaction.h"
#include "ui.h"
#include "util.h"
#include "xmb_installer_app.h"

static void auto_show(const char *title, const char *text, int ms);
static void power_off_now(void);
static int power_off_countdown(const char *title);

/* ------------------------------------------------------------------ */
/* Install progress screen                                             */

typedef struct {
  const game_plan_t *p;
  install_stage_t stage;
  int no_skip;     /* START does not skip the read-back (Verify game data) */
  int skip_verify; /* START was pressed during the read-back */
} progress_ctx_t;

#define ROW_STAGE 11
#define ROW_BAR 13

static void draw_install_static(const game_plan_t *p, const char *title) {
  ui_header(title, NULL);
  ui_at(3, " Title       %.60s", p->title);
  ui_at(4, " Startup ID  %s   (%s, %s)", p->iso.boot_id,
        p->iso.disc_type == DISC_TYPE_DVD ? "DVD" : "CD", source_type_label(p->type));
  ui_at(5, " Source      %.64s", p->source_path);
  ui_at(6, " Hidden      %s", p->hidden);
  ui_at(7, " Visible     %s", p->visible);
}

static void cb_stage(void *ctx, install_stage_t s) {
  progress_ctx_t *c = ctx;
  c->stage = s;
  ui_at(ROW_STAGE, " Stage: %s", install_stage_name(s));
}

static void cb_progress(void *ctx, uint64_t done, uint64_t total, uint32_t el) {
  const progress_ctx_t *c = ctx;
  char bar[52];
  int pct = total ? (int)((done * 100) / total) : 0;
  int fill = pct / 2;
  for (int i = 0; i < 50; i++)
    bar[i] = i < fill ? '#' : '.';
  bar[50] = 0;
  /* MiB/s x10 and ETA from 64-bit counters. */
  unsigned mbps10 = el ? (unsigned)(((done * 10) >> 20) / el) : 0;
  unsigned eta = (el && done) ? (unsigned)(((total - done) * (uint64_t)el) / done) : 0;
  ui_at(ROW_BAR, " [%s] %3d%%", bar, pct);
  ui_at(ROW_BAR + 1, " %llu / %llu bytes", (unsigned long long)done,
        (unsigned long long)total);
  ui_at(ROW_BAR + 2, " %u MiB of %u MiB   %u.%u MiB/s   elapsed %u:%02u   ETA %u:%02u",
        (unsigned)(done >> 20), (unsigned)(total >> 20), mbps10 / 10, mbps10 % 10,
        (unsigned)(el / 60), (unsigned)(el % 60), eta / 60, eta % 60);
  /* Speed of each step on its own: the slowest one limits the install. */
  const stream_timing_t *t = &g_stream_timing;
  unsigned rd = rate_mib10(t->bytes, t->read_ticks, STREAM_TIMER_HZ);
  unsigned cr = rate_mib10(t->bytes, t->crc_ticks, STREAM_TIMER_HZ);
  unsigned wr = rate_mib10(t->bytes, t->write_ticks, STREAM_TIMER_HZ);
  if (c && c->stage == STAGE_VALIDATING)
    ui_at(ROW_BAR + 3, " HDD read %u.%u   CRC %u.%u MiB/s", rd / 10, rd % 10, cr / 10,
          cr % 10);
  else
    ui_at(ROW_BAR + 3, " network %u.%u   CRC %u.%u   HDD write %u.%u MiB/s", rd / 10,
          rd % 10, cr / 10, cr % 10, wr / 10, wr % 10);
  if (c && c->stage == STAGE_VALIDATING && !c->no_skip)
    ui_at(ROW_BAR + 4, " [START] skip verification    Hold [SELECT]+[O] to abort");
  else
    ui_at(ROW_BAR + 4, " Hold [SELECT]+[O] to abort (no XMB channel will be created).");
}

static int cb_abort(void *ctx) {
  progress_ctx_t *c = ctx;
  int b = ui_poll_button();
  /* START during the read-back: skip it, keep the install. */
  if (c && c->stage == STAGE_VALIDATING && !c->no_skip && (b & UI_START)) {
    c->skip_verify = 1;
    return 1;
  }
  /* Two buttons so a stray press cannot abort a long copy. */
  return (b & UI_CIRCLE) && (ui_held_buttons() & UI_SELECT);
}

static int cb_skip_verify(void *ctx) {
  const progress_ctx_t *c = ctx;
  return c && c->skip_verify;
}

/* ------------------------------------------------------------------ */

void flow_show_error(const char *what, const install_report_t *rep,
                     const char *recovery) {
  char msg[1100];
  int off = snprintf(msg, sizeof(msg),
                     "%s\n\n"
                     "Stage:            %s\n"
                     "Error:            %s\n"
                     "                  (%s)\n"
                     "Driver code:      %d\n"
                     "Detail:           %s\n\n"
                     "Hidden game partition exists:  %s\n"
                     "Visible XMB channel exists:    %s\n",
                     what, install_stage_name(rep->stage), err_text(rep->err),
                     err_name(rep->err), rep->rc, rep->detail ? rep->detail : "-",
                     rep->hidden_exists ? "yes" : "no",
                     rep->visible_exists ? "yes" : "no");
  if (rep->bytes_written)
    off += snprintf(msg + off, sizeof(msg) - off,
                    "Copied %llu bytes, read back %llu, CRC %08lx / %s\n",
                    (unsigned long long)rep->bytes_written,
                    (unsigned long long)rep->bytes_verified,
                    (unsigned long)rep->source_crc32,
                    rep->have_crc ? "see journal" : "not verified");
  snprintf(msg + off, sizeof(msg) - off, "\nSafe recovery: %s", recovery);
  ui_message("Error", msg);
}

static const char *recovery_for(const install_report_t *rep) {
  if (rep->data_installed_no_channel)
    return "install OPL, then Repair XMB Channels > Create XMB channel.";
  if (rep->err == ERR_OPL_NOT_FOUND)
    return "install OPL on the HDD (+OPL or per __common/OPL/conf_hdd.cfg).";
  if (rep->err == ERR_KELF_MISSING)
    return "use a signed release build (make dist), not a dev build.";
  if (rep->err == ERR_HDL_PLAN)
    return "nothing was written; the game exceeds this drive's APA limits.";
  if (rep->stage <= STAGE_PREPARING)
    return "nothing was written; fix the cause and try again.";
  if (rep->stage >= STAGE_CREATING_CHANNEL)
    return "game data is kept; use Repair XMB Channels to retry the channel.";
  return "retry from start, or delete the incomplete install.";
}

static void run_install(game_plan_t *p, int allow_without_opl) {
  /* A resumed copy is always verified in full (no START skip). */
  progress_ctx_t ctx = {p, STAGE_PREPARING, p->resume, 0};
  install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx, cb_skip_verify};
  install_report_t rep;
  draw_install_static(p, "Installing");
  ui_footer("Do not power off.");
  game_install(p, allow_without_opl, &ui, &rep);

  if (rep.err == ERR_OK) {
    /* TX_COMPLETE reached: only now report success. */
    char msg[800], installed[64];
    if (rep.verify_skipped)
      snprintf(installed, sizeof(installed), "SKIPPED (Verify game data later)");
    else
      snprintf(installed, sizeof(installed), "%08lx", (unsigned long)rep.installed_crc32);
    if (rep.resumed_from)
      snprintf(installed + strlen(installed), sizeof(installed) - strlen(installed),
               "  (resumed at %lu MiB)", (unsigned long)(rep.resumed_from >> 20));
    snprintf(msg, sizeof(msg),
             "%s installed (TX_COMPLETE).\n\n%s\n%s\n\n"
             "Bytes copied:     %llu\nBytes read back:  %llu\n"
             "Source CRC-32:    %08lx\nInstalled CRC-32: %s\n"
             "OPL settings:     %s\n"
             "Cover:            %s\n\n"
             "The game appears as its own XMB channel\n"
             "after the XMB refreshes (return to the XMB or reboot).",
             p->title, p->visible, p->hidden, (unsigned long long)rep.bytes_written,
             (unsigned long long)rep.bytes_verified, (unsigned long)rep.source_crc32,
             installed, rep.opl_cfg ? rep.opl_cfg : "none",
             rep.jacket && !strcmp(rep.jacket, "missing")
                 ? "not found on server, default used"
                 : (rep.jacket ? rep.jacket : "none"));
    ui_message("Finished", msg);
    return;
  }
  flow_show_error("Install did not complete.", &rep, recovery_for(&rep));

  /* Copy-stage failures: offer Retry from start / Delete incomplete. */
  if (rep.hidden_exists && !rep.visible_exists && !rep.data_installed_no_channel &&
      rep.stage >= STAGE_CREATING_HDL && rep.stage <= STAGE_VALIDATING) {
    static char opts[3][UI_ROW_LEN] = {"Retry from start", "Delete incomplete install",
                                       "Keep it (repair later)"};
    int c = ui_select("Incomplete install", p->hidden, opts, 3, 0, NULL, NULL);
    if (c == 0 || c == 1) {
      const char *failed;
      int rc;
      char txt[200];
      snprintf(txt, sizeof(txt), "Delete incomplete hidden partition\n\n  %s\n", p->hidden);
      if (!ui_confirm_destructive("Delete incomplete install", txt))
        return;
      if (game_delete_pair(NULL, p->hidden, &failed, &rc) != ERR_OK) {
        snprintf(txt, sizeof(txt), "Could not delete %s (code %d).", failed, rc);
        ui_message("Error", txt);
        return;
      }
      if (c == 0)
        run_install(p, allow_without_opl);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Batch install of every game the server lists (manifest or /INSTALL) */

static batch_entry_t batch[BATCH_MAX];
static game_plan_t batch_plans[BATCH_MAX];
static char batch_rows[BATCH_MAX][UI_ROW_LEN];

/* "<dir>/<name>" into out; -1 if it does not fit. */
static int join_path(char *out, size_t outsz, const char *dir, const char *name) {
  size_t a = strlen(dir), b = strlen(name);
  if (a + 1 + b >= outsz)
    return -1;
  memcpy(out, dir, a);
  out[a] = '/';
  memcpy(out + a + 1, name, b + 1);
  return 0;
}

/* Collect candidate images under dir (and one level of subfolders such
 * as CD/ and DVD/, which also give the disc-type hint). */
static void batch_collect(const char *dir, int depth, int *n) {
  int dd = fileXioDopen(dir);
  if (dd < 0)
    return;
  static iox_dirent_t de;
  char subdirs[8][128];
  int nsub = 0;
  while (*n < BATCH_MAX && fileXioDread(dd, &de) > 0) {
    if (!strcmp(de.name, ".") || !strcmp(de.name, ".."))
      continue;
    char path[SOURCE_PATH_MAX];
    if (join_path(path, sizeof(path), dir, de.name) < 0)
      continue;
    if ((de.stat.mode & FIO_S_IFMT) == FIO_S_IFDIR) {
      if (depth > 0 && nsub < 8 && strlen(de.name) < sizeof(subdirs[0]))
        str_copy(subdirs[nsub++], de.name, sizeof(subdirs[0]));
      continue;
    }
    source_type_t t = source_classify(de.name);
    if (t == SRC_TYPE_NONE)
      continue;
    batch_entry_t *e = &batch[*n];
    memset(e, 0, sizeof(*e));
    str_copy(e->path, path, sizeof(e->path));
    str_copy(e->name, de.name, sizeof(e->name));
    e->type = t;
    (*n)++;
  }
  fileXioDclose(dd);
  for (int i = 0; i < nsub; i++) {
    char sub[SOURCE_PATH_MAX];
    if (join_path(sub, sizeof(sub), dir, subdirs[i]) == 0)
      batch_collect(sub, depth - 1, n);
  }
}

/* Copy the plan's names/size into entry i and read its pair state. */
static void batch_fill_from_plan(int i) {
  batch_entry_t *e = &batch[i];
  game_plan_t *p = &batch_plans[i];
  e->bytes = p->iso.source_size;
  str_copy(e->boot_id, p->iso.boot_id, sizeof(e->boot_id));
  str_copy(e->title, p->title, sizeof(e->title));
  str_copy(e->visible, p->visible, sizeof(e->visible));
  str_copy(e->hidden, p->hidden, sizeof(e->hidden));
  e->alloc_mb = p->alloc.total_mb;
  e->pair = PAIR_NONE;
  if (e->probe_err == ERR_OK) {
    pair_facts_t f;
    game_pair_facts(p->visible, p->hidden, &f);
    e->pair = pair_classify(&f);
    e->resumable = f.resumable;
  }
}

/* Classify after marking games whose ID is already on the HDD under any
 * title (an older release or another tool may have used another title). */
static void batch_classify_against_hdd(int n) {
  static hdd_part_t hp[256];
  static const char *names[256];
  int np = hdd_list(hp, 256);
  for (int i = 0; i < np; i++)
    names[i] = hp[i].name;
  batch_mark_on_hdd(batch, n, names, np > 0 ? np : 0);
  batch_classify(batch, n);
}

static void batch_probe(int n) {
  for (int i = 0; i < n; i++) {
    ui_at(4, " Checking %d/%d: %.60s", i + 1, n, batch[i].name);
    int rc = 0;
    batch[i].probe_err = game_plan_build(batch[i].path, &batch_plans[i], &rc);
    batch_fill_from_plan(i);
  }
  batch_classify_against_hdd(n);
}

/* Fill batch[] from udpfsd's manifest (every configured game folder, no
 * image probing), else by probing udpfs:/INSTALL. Returns the count. */
static int batch_load_entries(const char *title) {
  int n = 0;
  manifest_load();
  if (!g_manifest_loaded) {
    ui_header(title, "Reading " BATCH_DIR " ...");
    batch_collect(BATCH_DIR, 1, &n);
    ui_header(title, "Checking images (ISO9660 + SYSTEM.CNF) ...");
    batch_probe(n);
    return n;
  }
  ui_header(title, "Checking the server's game list against the HDD ...");
  for (int i = 0; i < g_manifest.n && n < BATCH_MAX; i++, n++) {
    batch_entry_from_manifest(&batch[n], &g_manifest.e[i]);
    memset(&batch_plans[n], 0, sizeof(batch_plans[n]));
    if (g_manifest.e[i].ok)
      batch[n].probe_err = game_plan_from_manifest(&g_manifest.e[i], &batch_plans[n]);
    ui_at(4, " %d/%d: %.60s", n + 1, g_manifest.n, batch[n].name);
    if (batch[n].probe_err == ERR_OK)
      batch_fill_from_plan(n);
  }
  batch_classify_against_hdd(n);
  return n;
}

/* OPL missing on the HDD: install the server's pinned OPL without asking
 * (it only adds files/the default +OPL partition, never overwrites).
 * Returns 1 when OPL is present afterwards. quiet: no result screen. */
static int ensure_opl(const char *title, int quiet, const char **why) {
  opl_runtime_t opl;
  int rc;
  const char *dummy;
  if (!why)
    why = &dummy;
  *why = NULL;
  if (opl_check_runtime(&opl, &rc) == ERR_OK)
    return 1;
  if (g_app.net != NETWORK_READY || !g_manifest_loaded || !g_manifest.has_opl) {
    *why = "the server offers no OPL (OPNPS2LD.ELF next to udpfsd)";
    return 0;
  }
  ui_header(title, "OPL not found - installing it from the server ...");
  ui_at(4, " Open PS2 Loader is needed to start the games.");
  ui_at(5, " Copying the server's OPNPS2LD.ELF to hdd0:%s ...", opl.partition);
  const char *detail = NULL;
  inst_err_t e = opl_install_from_server(&detail, &rc);
  *why = detail;
  if (!quiet) {
    char msg[300];
    if (e == ERR_OK)
      snprintf(msg, sizeof(msg), "OPL was installed from the server to hdd0:%s and\n"
                                 "verified (SHA-256). Games can now get XMB channels.",
               opl.partition);
    else
      snprintf(msg, sizeof(msg), "OPL could not be installed from the server:\n%s\n%s (code %d)",
               detail ? detail : "-", err_name(e), rc);
    ui_message(title, msg);
  }
  return e == ERR_OK;
}

/* Install every selected entry with the unchanged single-game install
 * (copy, CRC read-back, journal, channel). SELECT+O aborts a game. */
static void batch_run_selected(int n, int allow_without_opl, const char *label) {
  int idx = 0, total = batch_count_selected(batch, n), stop = 0;
  for (int i = 0; i < n; i++) {
    batch_entry_t *e = &batch[i];
    if (!e->selected)
      continue;
    idx++;
    if (stop) {
      e->result = BATCH_SKIPPED;
      continue;
    }
    char title[48];
    snprintf(title, sizeof(title), "%s %d/%d", label, idx, total);
    batch_plans[i].resume = e->status == BATCH_RESUME;
    progress_ctx_t ctx = {&batch_plans[i], STAGE_PREPARING, batch_plans[i].resume, 0};
    install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx, cb_skip_verify};
    install_report_t rep;
    draw_install_static(&batch_plans[i], title);
    ui_footer("Do not power off.");
    game_install(&batch_plans[i], allow_without_opl, &ui, &rep);
    e->err = rep.err;
    e->stage = install_stage_name(rep.stage);
    e->opl_cfg = rep.opl_cfg;
    e->jacket = rep.jacket;
    e->verify_skipped = rep.verify_skipped;
    e->result = rep.err == ERR_OK              ? BATCH_DONE
                : rep.data_installed_no_channel ? BATCH_DATA_ONLY
                                                : BATCH_FAILED;
    if (rep.err == ERR_USER_ABORT && idx < total)
      stop = ui_confirm(label, "Game aborted. Stop the remaining games too?");
  }
}

void flow_batch_install(void) {
  if (g_app.net != NETWORK_READY) {
    ui_message("Install All Games", network_not_ready_text());
    return;
  }
  int n = batch_load_entries("Install All Games");
  if (g_manifest_loaded && g_manifest.scanning) {
    ui_message("Install All Games",
               "The server is still reading its game folders.\n"
               "Try again in a moment (its window shows 'games ready').");
    return;
  }
  if (n == 0) {
    ui_message("Install All Games",
               "No .iso/.zso games found on the server.\n\n"
               "Set the game folders (dvd, cd, games, install) in udpfsd.cfg\n"
               "next to the udpfsd from dist/udpfsd/ and restart it.");
    return;
  }

  /* Selection: Square toggles, X starts, O backs out. */
  static listui_state_t ls;
  static lv_item_t items[BATCH_MAX];
  ls.item = 0;
  for (;;) {
    for (int i = 0; i < n; i++) {
      batch_format_row(&batch[i], batch_rows[i], UI_ROW_LEN);
      items[i].name = batch[i].title[0] ? batch[i].title : batch[i].name;
      items[i].size = batch[i].bytes;
      items[i].group = 0;
    }
    uint32_t free_mb = 0;
    hdd_space_mb(NULL, &free_mb, NULL);
    char status[96];
    snprintf(status, sizeof(status), "%d/%d selected, need %lu MiB, free %lu",
             batch_count_selected(batch, n), n, (unsigned long)batch_needed_mb(batch, n),
             (unsigned long)free_mb);
    int key = 0;
    int c = listui_pick("Install All Games", status, items, batch_rows, n, &ls,
                        "[Sq] toggle  [X] install  [O] back", UI_SQUARE, &key);
    if (c < 0)
      return;
    if (key & UI_SQUARE) {
      batch_toggle(&batch[c]);
      continue;
    }
    int count = batch_count_selected(batch, n);
    if (count == 0) {
      ui_message("Install All Games", "Nothing selected.");
      continue;
    }
    if (batch_needed_mb(batch, n) > free_mb) {
      ui_message("Install All Games",
                 "Not enough free space for the selected games.\nDeselect some with Square.");
      continue;
    }
    break;
  }

  int allow_without_opl = 0;
  opl_runtime_t opl;
  int orc;
  if (!ensure_opl("Install All Games", 0, NULL) && opl_check_runtime(&opl, &orc) != ERR_OK) {
    char txt[400];
    snprintf(txt, sizeof(txt),
             "OPL runtime not found (looked for %s on hdd0:%s).\n\n"
             "No XMB channel can be created without it. Copy and verify the\n"
             "game data of all selected games anyway (channels later via\n"
             "Repair XMB Channels)?",
             opl.elf_path, opl.partition);
    if (!ui_confirm("OPL runtime not found", txt))
      return;
    allow_without_opl = 1;
  }
  /* Confirm; Square toggles switching the console off when done. */
  int power_off = g_manifest_loaded && g_manifest.power_off;
  for (;;) {
    ui_header("Install All Games", NULL);
    ui_at(3, " Install %d game(s), one after another (%lu MiB).",
          batch_count_selected(batch, n), (unsigned long)batch_needed_mb(batch, n));
    ui_at(5, " Each game is copied, read back and CRC-checked before its XMB");
    ui_at(6, " channel is created. Hold SELECT + O to abort the current game.");
    ui_at(8, " When all games are done:  %s",
          power_off ? "switch the DESR OFF" : "stay on (show the summary)");
    ui_footer("[X] install   [Square] power off when done: on/off   [O] back");
    int b = ui_wait_button();
    if (b & (UI_CIRCLE | UI_TRIANGLE))
      return;
    if (b & UI_SQUARE)
      power_off = !power_off;
    if (b & UI_CROSS)
      break;
  }

  batch_run_selected(n, allow_without_opl, "Install All");
  static char summary[4096];
  batch_summary(batch, n, summary, sizeof(summary));
  if (power_off) {
    auto_show("Install All Games - summary", summary, 15000);
    if (power_off_countdown("Install All Games"))
      power_off_now();
  }
  ui_text_view("Install All Games - summary", summary);
}

/* Show text for up to ms (any button continues); used where nobody may
 * be at the console. */
static void auto_show(const char *title, const char *text, int ms) {
  ui_header(title, NULL);
  int row = 3;
  for (const char *p = text; *p && row < UI_ROWS - 2;) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    ui_at(row++, " %.*s", len > UI_COLS - 2 ? UI_COLS - 2 : len, p);
    p = nl ? nl + 1 : p + len;
  }
  ui_footer("[any button] continue");
  ui_wait_button_timeout(ms);
}

/* Switch the console off cleanly: no copy is running (callers are past
 * their install loop), PFS files closed and unmounted, HDD cache flushed,
 * DEV9 (HDD + network) off, then the power. */
static void power_off_now(void) {
  network_wait_idle();
  ui_header("Power off", "Switching the DESR off...");
  app_unmount();
  fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);
  fileXioDevctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0);
  fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0);
  ui_pad_close();
  poweroffShutdown();
  for (;;)
    ui_delay_ms(1000);
}

/* Last chance to keep the console on: any button cancels (returns 0). */
static int power_off_countdown(const char *title) {
  for (int s = 15; s > 0; s--) {
    char st[64];
    snprintf(st, sizeof(st), "Switching off in %d s - any button cancels", s);
    ui_header(title, st);
    ui_at(4, " All games are done. The DESR switches itself off.");
    if (ui_wait_button_timeout(1000))
      return 0;
  }
  return 1;
}

static void exit_to_system_menu(void) {
  network_wait_idle();
  app_unmount();
  ui_pad_close();
  LoadExecPS2("rom0:OSDSYS", 0, NULL);
}

void flow_auto_install(void) {
  /* 1. The server publishes "scanning=1" until its game list is ready
   * (it never serves the previous run's list meanwhile): wait, bounded. */
  for (int t = 0; auto_should_wait(g_manifest_loaded, &g_manifest) && t < 36; t++) {
    ui_header("Auto-install", "Waiting for the server's game list ... [O] cancel");
    ui_at(4, " The server is still reading its game folders (%d s).", t * 5);
    if (ui_wait_button_timeout(5000) & (UI_CIRCLE | UI_TRIANGLE))
      return;
    manifest_load();
  }
  if (auto_should_wait(g_manifest_loaded, &g_manifest)) {
    auto_show("Auto-install",
              "The server's game list is not ready after 3 minutes.\n"
              "Nothing was installed. Start the installer again when the\n"
              "server window shows 'games ready'.",
              15000);
    return;
  }
  if (!g_manifest.auto_install)
    return;

  /* 2. Countdown: nothing has to be pressed, O/Triangle cancels. */
  for (int s = 10; s > 0; s--) {
    char st[80];
    snprintf(st, sizeof(st), "Installing new games in %d s - [O] cancel", s);
    ui_header("Auto-install", st);
    ui_at(4, " The server lists %d game image(s).", g_manifest.n);
    ui_at(6, " Every game not yet on the HDD is copied, read back and");
    ui_at(7, " CRC-checked, then gets its own XMB channel. Nothing on the");
    ui_at(8, " HDD is deleted or overwritten.");
    if (ui_wait_button_timeout(1000) & (UI_CIRCLE | UI_TRIANGLE))
      return;
  }

  /* 3. First run: the installer partition holds the install journals.
   * Only a missing one is created; an existing one that did not mount
   * may be damaged and is left for the manual menus. */
  switch (auto_installer_step(g_app.app_exists, g_app.app_mounted)) {
  case AUTO_INSTALLER_OK:
    break;
  case AUTO_STOP:
    auto_show("Auto-install",
              INSTALLER_PARTITION " exists but could not be mounted.\n\n"
              "Auto-install stopped; nothing was changed. Use\n"
              "'Install Installer as XMB Channel' or Diagnostics.",
              15000);
    return;
  case AUTO_CREATE_INSTALLER: {
    ui_header("Auto-install", "Creating " INSTALLER_PARTITION " ...");
    selfinstall_report_t sr;
    installer_app_install(&sr);
    if (sr.err || !g_app.app_mounted) {
      char msg[300];
      snprintf(msg, sizeof(msg),
               "Could not create %s:\n%s (%s), step: %s\n\n"
               "Auto-install stopped; no game was installed.",
               INSTALLER_PARTITION, err_text(sr.err), err_name(sr.err),
               sr.detail ? sr.detail : "-");
      auto_show("Auto-install", msg, 15000);
      return;
    }
    break;
  }
  }

  /* 4. Never create channel-less games here: OPL must be present. If it
   * is missing, the server's pinned OPL is installed first. */
  opl_runtime_t opl;
  int orc;
  const char *why = NULL;
  if (!ensure_opl("Auto-install", 1, &why) && opl_check_runtime(&opl, &orc) != ERR_OK) {
    char msg[400];
    snprintf(msg, sizeof(msg),
             "OPL runtime not found (looked for %s on hdd0:%s)\n"
             "and the server's OPL could not be installed: %s\n\n"
             "Auto-install did not install anything.",
             opl.elf_path, opl.partition,
             why ? why : "the server offers no OPL (OPNPS2LD.ELF next to udpfsd)");
    auto_show("Auto-install", msg, 15000);
    return;
  }

  /* 5. Every new game that fits, one after another. */
  int n = batch_load_entries("Auto-install");
  uint32_t free_mb = 0;
  hdd_space_mb(NULL, &free_mb, NULL);
  if (batch_auto_select(batch, n, free_mb) == 0) {
    /* Nothing to do: stay in the menu (the app was probably opened on
     * purpose, e.g. to manage installed games). */
    auto_show("Auto-install", "No new games to install.", 3000);
    return;
  }
  batch_run_selected(n, 0, "Auto-install");

  /* 6. Summary, then back to the XMB where the new channels appear. */
  static char summary[4096];
  batch_summary(batch, n, summary, sizeof(summary));
  auto_show("Auto-install finished", summary, 15000);
  /* udpfsd.cfg power_off_after_install = yes: switch off (cancellable). */
  if (g_manifest.power_off && power_off_countdown("Auto-install finished"))
    power_off_now();
  exit_to_system_menu();
}

void flow_install_game(game_plan_t *p) {
  for (;;) {
    pair_facts_t f;
    game_pair_facts(p->visible, p->hidden, &f);
    pair_state_t st = pair_classify(&f);
    if (st != PAIR_NONE) {
      flow_pair_actions(p->visible, p->hidden);
      /* After a delete the pair may now be free for a fresh install. */
      game_pair_facts(p->visible, p->hidden, &f);
      st = pair_classify(&f);
      if (st != PAIR_NONE)
        return;
    }

    char sz[32], status[96];
    snprintf(sz, sizeof(sz), "%u MiB", (unsigned)(((uint64_t)p->iso.sectors * 2048) >> 20));
    snprintf(status, sizeof(status), "%s  %s  %s", p->iso.boot_id, sz,
             region_label(p->iso.boot_id));
    draw_install_static(p, "Install game");
    ui_at(9, " Volume ID   %s", p->iso.volume_id);
    ui_at(10, " Size        %s (%u sectors)%s", sz, (unsigned)p->iso.sectors,
          p->iso.layer1_start ? "  DVD9" : "");
    ui_at(11, " Allocation  %s main + %d sub partition(s), %u MiB total + 128 MiB channel",
          p->alloc.main_size_str, p->alloc.subs, (unsigned)p->alloc.total_mb);
    ui_at(12, " Validation  ISO9660 PVD ok, SYSTEM.CNF ok, BOOT2 %.40s", p->iso.boot2);
    ui_at(13, " Region      %s     Install state: %s", region_label(p->iso.boot_id),
          pair_state_label(st));
    ui_at(15, " The data is copied to the hidden partition, read back in full and");
    ui_at(16, " CRC-checked before the visible XMB channel is created.");
    ui_footer("[X] install  [Square] edit title  [O] back");

    int b;
    do
      b = ui_wait_button();
    while (!(b & (UI_CROSS | UI_SQUARE | UI_CIRCLE | UI_TRIANGLE)));
    if (b & (UI_CIRCLE | UI_TRIANGLE))
      return;
    if (b & UI_SQUARE) {
      char t[64];
      str_copy(t, p->title, sizeof(t));
      if (ui_edit_text("Edit title", "Display title (startup ID cannot be changed):", t,
                       48) &&
          game_plan_set_title(p, t) != ERR_OK)
        ui_message("Title", "Invalid title.");
      continue;
    }

    int allow_without_opl = 0;
    opl_runtime_t opl;
    int rc;
    if (!ensure_opl("Install game", 0, NULL) && opl_check_runtime(&opl, &rc) != ERR_OK) {
      char txt[700];
      snprintf(txt, sizeof(txt),
               "OPL runtime not found (looked for %s on hdd0:%s).\n\n"
               "Without OPL the XMB channel would not boot, so it will NOT be\n"
               "created. You can still copy the game data now and create the\n"
               "channel later from Repair XMB Channels once OPL is installed.\n\n"
               "Copy game data only?",
               opl.elf_path, opl.partition);
      if (!ui_confirm("OPL runtime not found", txt))
        return;
      allow_without_opl = 1;
    }
    run_install(p, allow_without_opl);
    return;
  }
}

/* ------------------------------------------------------------------ */
/* Pair actions                                                         */

static void do_delete(const char *visible, const char *hidden, int pp_only,
                      int hidden_only) {
  char txt[400];
  snprintf(txt, sizeof(txt),
           "The following partition(s) will be PERMANENTLY removed:\n\n%s%s%s%s%s\n"
           "%s",
           hidden_only ? "" : "  ", hidden_only ? "" : visible, hidden_only ? "" : "\n",
           pp_only ? "" : "  ", pp_only ? "" : hidden,
           pp_only ? "" : "\nThe visible channel is removed first, then the game data.");
  if (!ui_confirm_destructive("Delete", txt))
    return;
  const char *failed;
  int rc;
  inst_err_t e = game_delete_pair(hidden_only ? NULL : visible, pp_only ? NULL : hidden,
                                  &failed, &rc);
  if (e) {
    snprintf(txt, sizeof(txt), "Could not remove %s (code %d).\n\n%s", failed, rc,
             failed == hidden ? "The XMB channel is already gone, so nothing broken\n"
                                "is visible in the XMB. Try deleting again."
                              : "Nothing else was changed.");
    ui_message("Delete failed", txt);
  } else {
    ui_message("Deleted", "Done.");
  }
}

static void do_create_channel(const char *hidden) {
  progress_ctx_t ctx = {NULL, STAGE_VALIDATING, 1, 0};
  install_ui_t ui = {cb_stage, NULL, NULL, &ctx, NULL};
  install_report_t rep;
  ensure_opl("Create/Repair XMB Channel", 0, NULL);
  ui_header("Create/Repair XMB Channel", hidden);
  game_create_channel(hidden, &ui, &rep);
  if (rep.err)
    flow_show_error("XMB channel was not created.", &rep, recovery_for(&rep));
  else {
    char msg[300];
    snprintf(msg, sizeof(msg),
             "The channel was created and verified.\n"
             "It appears after the XMB refreshes. Game data was not rewritten.\n\n"
             "Cover: %s",
             rep.jacket && !strcmp(rep.jacket, "missing")
                 ? "not found on server, default used (restart udpfsd)"
                 : (rep.jacket ? rep.jacket : "none"));
    ui_message("XMB channel created", msg);
  }
}

#define ROW_VERIFY (1 << 16) /* menu rows, not pair_action_t values */
#define ROW_RENAME (1 << 17)
#define ROW_RESUME (1 << 18)
#define ROW_BACKUP (1 << 19)

/* ps1: "name" is the PS1 channel, else the hidden PS2 partition. */
static void do_backup(const char *name, int ps1) {
  if (!g_app.iop.usb_ok) {
    ui_message("Back up to USB", "The USB drivers failed to load (see Diagnostics).");
    return;
  }
  char txt[300];
  snprintf(txt, sizeof(txt),
           "Copy %s to the USB drive (%s/%s folder)?\n\n"
           "Use a FAT32 or exFAT drive; games over 4 GiB need exFAT.\n"
           "The copy is checked against the HDD data afterwards.",
           name + 3, USB_ROOT, ps1 ? "POPS" : "DVD or CD");
  if (!ui_confirm("Back up to USB", txt))
    return;
  progress_ctx_t ctx = {NULL, STAGE_PREPARING, 0, 0};
  install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx, cb_skip_verify};
  install_report_t rep;
  ui_header("Back up to USB", name);
  ui_footer("Do not unplug the USB drive.");
  if (ps1)
    backup_ps1_game(name, &ui, &rep);
  else
    backup_ps2_game(name, &ui, &rep);
  if (rep.err == ERR_USER_ABORT) {
    ui_message("Back up to USB", "Stopped. The partial file was removed.");
  } else if (rep.err) {
    flow_show_error("Backup did not complete.", &rep,
                    "the partial file was removed; the game on the HDD is unchanged.");
  } else {
    snprintf(txt, sizeof(txt), "Saved as\n  %s\n\n%llu bytes, CRC-32 %08lx, read back: %s.",
             rep.detail, (unsigned long long)rep.bytes_written, (unsigned long)rep.source_crc32,
             rep.verify_skipped ? "SKIPPED" : "equal");
    ui_message("Back up to USB", txt);
  }
}


static void do_resume(const char *hidden) {
  if (g_app.net != NETWORK_READY) {
    ui_message("Resume copy", network_not_ready_text());
    return;
  }
  static game_plan_t plan;
  int rc = 0;
  ui_header("Resume copy", hidden);
  ui_at(4, " Checking the image on the server...");
  inst_err_t e = game_resume_plan(hidden, &plan, &rc);
  if (e) {
    char msg[400];
    snprintf(msg, sizeof(msg),
             "Cannot resume: %s (%s), code %d.\n\n"
             "The game image must still be on the server at the same path.\n"
             "Otherwise delete the incomplete install and install it again.",
             err_text(e), err_name(e), rc);
    ui_message("Resume copy", msg);
    return;
  }
  int allow_without_opl = 0;
  opl_runtime_t opl;
  int orc;
  if (!ensure_opl("Resume copy", 0, NULL) && opl_check_runtime(&opl, &orc) != ERR_OK) {
    if (!ui_confirm("OPL runtime not found",
                    "OPL is missing, so no XMB channel can be created.\n"
                    "Finish copying the game data anyway?"))
      return;
    allow_without_opl = 1;
  }
  run_install(&plan, allow_without_opl);
}

static void do_rename(const char *visible) {
  char t[64] = "";
  if (channel_get_title(visible, t, sizeof(t)) < 0)
    str_copy(t, visible + 3 + PART_ID_LEN + 2, sizeof(t)); /* "..TITLE" part */
  if (!ui_edit_text("Rename", "Title shown in the XMB (the game ID stays the same):", t, 48))
    return;
  ui_header("Rename", visible);
  channel_result_t r = channel_set_title(visible, t);
  char msg[300];
  if (r.err)
    snprintf(msg, sizeof(msg),
             "The title was not changed: %s (%s), step %s, code %d.\n\n"
             "If the channel now shows no title, use Repair XMB channel.",
             err_text(r.err), err_name(r.err), r.step ? r.step : "-", r.rc);
  else
    snprintf(msg, sizeof(msg),
             "New title: %s\n\nIt appears after the XMB refreshes (return to the\n"
             "XMB or reboot). The partition name and game data are unchanged.",
             t);
  ui_message("Rename", msg);
}

static void do_verify(const char *visible, const char *hidden) {
  progress_ctx_t ctx = {NULL, STAGE_VALIDATING, 1, 0};
  install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx, NULL};
  install_report_t rep;
  ui_header("Verify game data", hidden);
  ui_at(4, " Reading back all installed data and comparing its CRC-32 with the");
  ui_at(5, " CRC-32 recorded while copying. Nothing is written except the journal.");
  game_verify_data(hidden, &ui, &rep);
  if (rep.err == ERR_USER_ABORT) {
    ui_message("Verify game data", "Stopped. Nothing was changed.");
  } else if (rep.err) {
    flow_show_error(rep.detail && !strcmp(rep.detail, "CRC-32 of installed data != source stream")
                        ? "The installed data does NOT match the copy (CRC differs)."
                        : "Verification did not run to the end.",
                    &rep,
                    rep.err == ERR_HDL_VERIFY && rep.have_crc
                        ? "reinstall the game (Delete game, then install it again)."
                        : "nothing was changed; try again.");
  } else {
    char msg[300];
    snprintf(msg, sizeof(msg),
             "Verified: %llu bytes read back,\nCRC-32 %08lx equals the copy's CRC-32.\n\n%s",
             (unsigned long long)rep.bytes_verified, (unsigned long)rep.installed_crc32,
             visible);
    ui_message("Verify game data", msg);
  }
}

void flow_pair_actions(const char *visible, const char *hidden) {
  pair_facts_t f;
  game_pair_facts(visible, hidden, &f);
  pair_state_t st = pair_classify(&f);
  unsigned acts = pair_actions(st);

  static char rows[11][UI_ROW_LEN];
  int map[11], n = 0;
#define ADD(a, label)                                                          \
  if (acts & (a)) {                                                            \
    str_copy(rows[n], label, UI_ROW_LEN);                                      \
    map[n++] = (a);                                                            \
  }
  ADD(ACT_CREATE_CHANNEL, st == PAIR_HIDDEN_ONLY ? "Create XMB channel"
                          : st == PAIR_COMPLETE  ? "Repair XMB channel"
                                                 : "Rebuild XMB channel");
  ADD(ACT_REMOVE_CHANNEL, st == PAIR_ORPHAN_CHANNEL ? "Remove broken channel"
                                                    : "Remove channel (game data invalid)");
  ADD(ACT_DELETE_INCOMPLETE, "Delete incomplete game");
  ADD(ACT_REINSTALL, "Reinstall game (delete, then copy again)");
  ADD(ACT_DELETE, "Delete game");
#undef ADD
  if (f.resumable) {
    snprintf(rows[n], UI_ROW_LEN, "Resume copy (%lu MiB already on the HDD)",
             (unsigned long)(f.resume_bytes >> 20));
    map[n++] = ROW_RESUME;
  }
  if (f.visible_exists && f.visible_valid) {
    str_copy(rows[n], "Rename (title shown in the XMB)", UI_ROW_LEN);
    map[n++] = ROW_RENAME;
  }
  if (f.hidden_exists && f.hidden_header_valid) {
    str_copy(rows[n], "Back up to USB (.iso)", UI_ROW_LEN);
    map[n++] = ROW_BACKUP;
  }
  if (pair_can_verify(&f)) {
    str_copy(rows[n], f.verify_skipped ? "Verify game data (was skipped)"
                                       : "Verify game data again",
             UI_ROW_LEN);
    map[n++] = ROW_VERIFY;
  }
  str_copy(rows[n], "Details (why this state)", UI_ROW_LEN);
  map[n++] = 0;

  char status[96];
  snprintf(status, sizeof(status), "State: %s", pair_label(&f));
  ui_header("Existing installation", status);
  int c = ui_select("Existing installation", status, rows, n, 0, NULL, NULL);
  if (c < 0)
    return;
  switch (map[c]) {
  case ROW_VERIFY:
    do_verify(visible, hidden);
    break;
  case ROW_RENAME:
    do_rename(visible);
    break;
  case ROW_RESUME:
    do_resume(hidden);
    break;
  case ROW_BACKUP:
    do_backup(hidden, 0);
    break;
  case 0: {
    static char details[2048];
    game_pair_details(visible, hidden, details, sizeof(details));
    ui_text_view("Details", details);
    break;
  }
  case ACT_CREATE_CHANNEL:
    do_create_channel(hidden);
    break;
  case ACT_REMOVE_CHANNEL:
    do_delete(visible, hidden, 1, 0);
    break;
  case ACT_DELETE_INCOMPLETE:
    do_delete(visible, hidden, 0, 1);
    break;
  case ACT_REINSTALL:
    /* Delete here; flow_install_game continues with a fresh install
     * if the pair is now free. From the manage menu, the user picks
     * the source in the browser afterwards. */
    do_delete(visible, hidden, 0, !f.visible_exists);
    break;
  case ACT_DELETE:
    do_delete(visible, hidden, !f.hidden_exists, !f.visible_exists);
    break;
  }
}

/* ------------------------------------------------------------------ */
/* PS1 games (POPStarter)                                              */

void flow_install_ps1(const char *path) {
  static pops_plan_t plan;
  int rc = 0;
  ui_header("Checking", path);
  ui_at(4, " Reading the VCD (ISO9660 + SYSTEM.CNF)...");
  inst_err_t e = pops_plan_build(path, &plan, &rc);
  if (e) {
    char msg[500];
    snprintf(msg, sizeof(msg),
             "%s\n\n%s (%s), code %d.\n\n"
             "Only PS1 games converted to .VCD (POPStarter format, e.g. with\n"
             "cue2pops) are supported. Nothing was written to the HDD.",
             path, err_text(e), err_name(e), rc);
    ui_message("Cannot install", msg);
    return;
  }
  for (;;) {
    ui_header("Install PS1 game", NULL);
    ui_at(3, " Title       %.60s", plan.title);
    ui_at(4, " Game ID     %s   (PS1, POPStarter)", plan.vcd.boot_id);
    ui_at(5, " Source      %.64s", plan.source_path);
    ui_at(6, " Partition   %s (%s)", plan.partition, plan.size_str);
    ui_at(7, " Size        %lu MiB", (unsigned long)(plan.vcd.bytes >> 20));
    ui_at(9, " Needs POPSTARTER.KELF next to the VCD or in POPS/ on the same");
    ui_at(10, " device; POPS.ELF and IOPRP252.IMG are copied to __common/POPS");
    ui_at(11, " if they are not there yet.");
    ui_footer("[X] install  [Square] edit title  [O] back");
    int b;
    do
      b = ui_wait_button();
    while (!(b & (UI_CROSS | UI_SQUARE | UI_CIRCLE | UI_TRIANGLE)));
    if (b & (UI_CIRCLE | UI_TRIANGLE))
      return;
    if (b & UI_SQUARE) {
      char t[64];
      str_copy(t, plan.title, sizeof(t));
      if (ui_edit_text("Edit title", "Display title (game ID cannot be changed):", t, 48) &&
          pops_plan_set_title(&plan, t) != ERR_OK)
        ui_message("Title", "Invalid title.");
      continue;
    }
    break;
  }
  progress_ctx_t ctx = {NULL, STAGE_PREPARING, 0, 0};
  install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx, cb_skip_verify};
  install_report_t rep;
  ui_header("Installing PS1 game", plan.partition);
  ui_at(3, " Title       %.60s", plan.title);
  ui_at(4, " Game ID     %s", plan.vcd.boot_id);
  ui_footer("Do not power off.");
  pops_install(&plan, &ui, &rep);
  if (rep.err) {
    flow_show_error("PS1 install did not complete.", &rep,
                    rep.visible_exists ? "delete the game with Remove Games, then retry."
                                       : "nothing is left on the HDD; fix the cause and retry.");
    return;
  }
  char msg[400];
  snprintf(msg, sizeof(msg),
           "%s installed as %s.\n\n"
           "Copied %llu bytes, CRC-32 %08lx, read back: %s.\n\n"
           "It appears in the XMB after it refreshes and starts through\n"
           "POPStarter. Memory cards: __common/POPS/%s/",
           plan.title, plan.partition, (unsigned long long)rep.bytes_written,
           (unsigned long)rep.source_crc32, rep.verify_skipped ? "SKIPPED" : "equal",
           plan.partition + 3);
  ui_message("Finished", msg);
}

void flow_ps1_actions(const char *partition) {
  static char rows[4][UI_ROW_LEN] = {"Rename (title shown in the XMB)",
                                     "Back up to USB (.VCD)", "Delete game", "Back"};
  int c = ui_select("PS1 game", partition, rows, 4, 0, NULL, NULL);
  if (c == 0) {
    do_rename(partition);
  } else if (c == 1) {
    do_backup(partition, 1);
  } else if (c == 2) {
    char txt[200];
    snprintf(txt, sizeof(txt), "The PS1 game partition will be PERMANENTLY removed:\n\n  %s\n\n"
                               "Its memory cards in __common/POPS stay.", partition);
    if (!ui_confirm_destructive("Delete", txt))
      return;
    const char *failed;
    int rc;
    if (game_delete_pair(partition, NULL, &failed, &rc) != ERR_OK) {
      snprintf(txt, sizeof(txt), "Could not remove %s (code %d).", partition, rc);
      ui_message("Delete failed", txt);
    } else {
      ui_message("Deleted", "Done.");
    }
  }
}

/* ------------------------------------------------------------------ */
/* Installed games / repair                                            */

#define MAX_PAIRS 128
typedef struct {
  char visible[APA_NAME_MAX + 1];
  char hidden[APA_NAME_MAX + 1];
  pair_state_t state;
} pair_row_t;

static pair_row_t pairs[MAX_PAIRS];
static char pair_rows[MAX_PAIRS][UI_ROW_LEN];
static lv_item_t pair_items[MAX_PAIRS];
static listui_state_t rm_ls;
static hdd_part_t parts[256];

static int collect_pairs(void) {
  ui_header("Scanning", "Reading APA partition table...");
  int np = hdd_list(parts, 256), n = 0;
  if (np < 0)
    return np;
  for (int i = 0; i < np && n < MAX_PAIRS; i++) {
    const char *name = parts[i].name;
    char partner[APA_NAME_MAX + 1];
    /* Hidden games must be HDL; channels must be PFS. hdl-dump's
     * visible installs ("PP." of type HDL) are left alone. */
    if (partition_is_hidden_game(name) && parts[i].type == APA_TYPE_HDL_ID) {
      partition_partner(name, partner);
      str_copy(pairs[n].hidden, name, sizeof(pairs[n].hidden));
      str_copy(pairs[n].visible, partner, sizeof(pairs[n].visible));
      n++;
    } else if (partition_is_xmb_channel(name, parts[i].type)) {
      partition_partner(name, partner);
      int have = 0;
      for (int k = 0; k < np; k++)
        if (strcmp(parts[k].name, partner) == 0)
          have = 1;
      if (!have) { /* orphan; paired ones are added via the hidden side */
        str_copy(pairs[n].visible, name, sizeof(pairs[n].visible));
        str_copy(pairs[n].hidden, partner, sizeof(pairs[n].hidden));
        n++;
      }
    }
  }
  for (int i = 0; i < n; i++) {
    pair_facts_t f;
    ui_at(4, " Checking %d/%d: %s", i + 1, n, pairs[i].hidden + 3);
    game_pair_facts(pairs[i].visible, pairs[i].hidden, &f);
    pairs[i].state = pair_classify(&f);
    /* A channel without a __. partner may be a PS1 game (IMAGE0.VCD). */
    if (pairs[i].state == PAIR_ORPHAN_CHANNEL && pops_partition_is_ps1(pairs[i].visible))
      pairs[i].state = PAIR_PS1;
    snprintf(pair_rows[i], UI_ROW_LEN, "%-34.34s %s", pairs[i].visible + 3,
             pairs[i].state == PAIR_PS1 ? pair_state_label(PAIR_PS1) : pair_label(&f));
  }
  return n;
}

static void pair_list(const char *title, int only_problems) {
  int sel = 0;
  for (;;) {
    int n = collect_pairs();
    if (n < 0) {
      ui_message(title, "Cannot read the HDD partition table.");
      return;
    }
    if (only_problems) {
      int m = 0;
      for (int i = 0; i < n; i++)
        if (pairs[i].state != PAIR_COMPLETE) {
          pairs[m] = pairs[i];
          memcpy(pair_rows[m], pair_rows[i], UI_ROW_LEN);
          m++;
        }
      n = m;
    }
    char status[96];
    snprintf(status, sizeof(status), "%d game%s%s", n, n == 1 ? "" : "s",
             only_problems ? " need attention" : "");
    for (int i = 0; i < n; i++) {
      pair_items[i].name = pairs[i].visible + 3; /* "SLUS-20312..TITLE" */
      pair_items[i].size = 0;
      /* Games that need attention first. */
      pair_items[i].group = pairs[i].state == PAIR_COMPLETE ? 1 : 0;
    }
    static listui_state_t ls;
    ls.item = sel;
    int c = listui_pick(title, status, pair_items, pair_rows, n, &ls, NULL, 0, NULL);
    if (c < 0)
      return;
    sel = c;
    if (pairs[c].state == PAIR_PS1)
      flow_ps1_actions(pairs[c].visible);
    else
      flow_pair_actions(pairs[c].visible, pairs[c].hidden);
  }
}

void flow_installed_games(void) { pair_list("Installed Games", 0); }

/* ------------------------------------------------------------------ */
/* Remove games (several at once)                                      */

static remove_entry_t rm[MAX_PAIRS];
static char rm_rows[MAX_PAIRS][UI_ROW_LEN];

void flow_remove_games(void) {
  int n = collect_pairs();
  if (n < 0) {
    ui_message("Remove Games", "Cannot read the HDD partition table.");
    return;
  }
  if (n == 0) {
    ui_message("Remove Games", "No installed games found.");
    return;
  }
  for (int i = 0; i < n; i++)
    remove_entry_init(&rm[i], pairs[i].visible, pairs[i].hidden, pairs[i].state);

  /* Selection: Square toggles, Start selects all/none, X removes. */
  int sel = 0;
  for (;;) {
    for (int i = 0; i < n; i++)
      remove_format_row(&rm[i], rm_rows[i], UI_ROW_LEN);
    uint32_t free_mb = 0;
    hdd_space_mb(NULL, &free_mb, NULL);
    char status[96];
    snprintf(status, sizeof(status), "%d of %d selected, free %lu MiB",
             remove_count_selected(rm, n), n, (unsigned long)free_mb);
    int key = 0;
    for (int i = 0; i < n; i++) {
      pair_items[i].name = rm[i].visible + 3;
      pair_items[i].size = 0;
      pair_items[i].group = 0;
    }
    rm_ls.item = sel;
    int c = listui_pick("Remove Games", status, pair_items, rm_rows, n, &rm_ls,
                        "[Sq] toggle [Start] all [X] remove", UI_SQUARE | UI_START, &key);
    if (c < 0)
      return;
    sel = c;
    if (key & UI_SQUARE) {
      remove_toggle(&rm[c]);
      continue;
    }
    if (key & UI_START) {
      remove_toggle_all(rm, n);
      continue;
    }
    if (remove_count_selected(rm, n) == 0) {
      ui_message("Remove Games", "Nothing selected. Square selects a game.");
      continue;
    }
    break;
  }

  int count = remove_count_selected(rm, n), listed = 0;
  char txt[900];
  int off = snprintf(txt, sizeof(txt),
                     "%d game(s) will be PERMANENTLY removed (XMB channel and\n"
                     "game data partitions):\n\n",
                     count);
  for (int i = 0; i < n && listed < 10; i++)
    if (rm[i].selected) {
      off += snprintf(txt + off, sizeof(txt) - off, "  %.50s\n", rm[i].visible + 3);
      listed++;
    }
  if (count > listed)
    snprintf(txt + off, sizeof(txt) - off, "  ... and %d more\n", count - listed);
  if (!ui_confirm_destructive("Remove Games", txt))
    return;

  int idx = 0;
  for (int i = 0; i < n; i++) {
    remove_entry_t *e = &rm[i];
    if (!e->selected)
      continue;
    char st[64];
    snprintf(st, sizeof(st), "Removing %d/%d - do not power off", ++idx, count);
    ui_header("Remove Games", st);
    ui_at(4, " %s", e->visible + 3);
    e->err = game_delete_pair(e->visible, e->hidden, &e->failed, &e->rc);
    /* Check with the partition table that both are really gone. */
    if (e->err == ERR_OK && hdd_exists(e->visible) != 0) {
      e->err = ERR_PARTITION_DELETE;
      e->failed = e->visible;
    } else if (e->err == ERR_OK && hdd_exists(e->hidden) != 0) {
      e->err = ERR_PARTITION_DELETE;
      e->failed = e->hidden;
    }
    e->result = e->err == ERR_OK ? REMOVE_DONE : REMOVE_FAILED;
  }

  static char summary[4096];
  size_t len = remove_summary(rm, n, summary, sizeof(summary));
  uint32_t free_mb = 0;
  hdd_space_mb(NULL, &free_mb, NULL);
  snprintf(summary + len, sizeof(summary) - len, "\nFree space now: %lu MiB\n",
           (unsigned long)free_mb);
  ui_text_view("Remove Games - summary", summary);
}

void flow_repair(void) {
  if (g_app.app_mounted) {
    static tx_journal_t txs[16];
    int n = tx_scan_unfinished(APP_STATE_DIR, txs, 16);
    if (n > 0) {
      char msg[900];
      int off = snprintf(msg, sizeof(msg), "%d unfinished transaction(s):\n\n", n);
      for (int i = 0; i < n && off < (int)sizeof(msg) - 80; i++)
        off += snprintf(msg + off, sizeof(msg) - off, " %s  %s  %s\n",
                        txs[i].startup_id, tx_state_name(txs[i].state),
                        txs[i].last_error);
      snprintf(msg + off, sizeof(msg) - off,
               "\nThey are listed below with their recovery actions.");
      ui_message("Repair XMB Channels", msg);
    }
  }
  pair_list("Repair XMB Channels", 1);
}

/* ------------------------------------------------------------------ */

void flow_network_settings(void) {
  static char rows[6][UI_ROW_LEN];
  for (;;) {
    snprintf(rows[4], UI_ROW_LEN, "Copy engine: %s",
             !g_app.iop.pump_ok        ? "basic (hddpump module not loaded)"
             : g_app.settings.fast_copy ? "fast (network and HDD overlap)"
                                        : "basic (one step after another)");
    snprintf(rows[0], UI_ROW_LEN, "IP address:  %s",
             g_app.settings.dhcp ? "automatic (DHCP from the router)" : "fixed (static)");
    snprintf(rows[1], UI_ROW_LEN, "%s  %s", g_app.settings.dhcp ? "Fallback IP:" : "Local IP:   ",
             g_app.settings.local_ip);
    snprintf(rows[2], UI_ROW_LEN, "Save and restart network");
    snprintf(rows[3], UI_ROW_LEN, "Restart network (retry discovery)");
    int c = ui_select("Network Settings", network_status_line(), rows, 5, 0, NULL, NULL);
    if (c < 0)
      return;
    if (c == 4) {
      /* Takes effect now; Save keeps it. */
      g_app.settings.fast_copy = !g_app.settings.fast_copy;
      g_hdl_use_pump = g_app.iop.pump_ok && g_app.settings.fast_copy;
      continue;
    }
    if (c == 0) {
      g_app.settings.dhcp = !g_app.settings.dhcp;
      continue;
    }
    c--; /* the rows below keep their former numbers */
    if (c == 0) {
      char ip[16];
      str_copy(ip, g_app.settings.local_ip, sizeof(ip));
      int oct = 0;
      ui_header("Edit IP", g_app.settings.dhcp ? "Used when no DHCP server answers"
                                                 : "Static IP of this console");
      ui_footer("[L/R] octet  [U/D] +1/-1  [L1/R1] -10/+10  [X] ok  [O] cancel");
      for (;;) {
        int o[4];
        sscanf(ip, "%d.%d.%d.%d", &o[0], &o[1], &o[2], &o[3]);
        char line[64];
        int pos = snprintf(line, sizeof(line), " ");
        for (int k = 0; k < 4; k++)
          pos += snprintf(line + pos, sizeof(line) - pos, k == oct ? "[%3d]%s" : " %3d %s",
                          o[k], k < 3 ? "." : "");
        ui_at(6, "%s", line);
        ui_at(8, " %s", ip_is_valid(ip) ? "valid host address" : "NOT a usable host address");
        int b = ui_wait_button();
        if (b & UI_LEFT)
          oct = (oct + 3) % 4;
        else if (b & UI_RIGHT)
          oct = (oct + 1) % 4;
        else if (b & UI_UP)
          ip_adjust_octet(ip, oct, 1);
        else if (b & UI_DOWN)
          ip_adjust_octet(ip, oct, -1);
        else if (b & UI_R1)
          ip_adjust_octet(ip, oct, 10);
        else if (b & UI_L1)
          ip_adjust_octet(ip, oct, -10);
        else if (b & UI_CROSS) {
          if (ip_is_valid(ip)) {
            str_copy(g_app.settings.local_ip, ip, sizeof(g_app.settings.local_ip));
            g_app.settings.using_default = 0;
            g_app.settings.warning = 0;
          }
          break;
        } else if (b & (UI_CIRCLE | UI_TRIANGLE))
          break;
      }
    } else if (c == 1) {
      if (g_app.app_mounted) {
        if (settings_save(APP_NETWORK_INI, &g_app.settings) != ERR_OK) {
          fileXioMkdir(APP_CONFIG_DIR, 0777);
          settings_save(APP_NETWORK_INI, &g_app.settings);
        }
      } else {
        ui_message("Network Settings",
                   "The installer partition does not exist yet, so the IP is used\n"
                   "for this session only. Install the installer XMB app to keep it.");
      }
      network_restart();
    } else if (c == 2) {
      network_restart();
    }
  }
}

void flow_self_install(void) {
  char txt[600];
  snprintf(txt, sizeof(txt),
           "Create or repair the installer's own XMB channel:\n\n"
           "  %s (128 MiB PFS)\n\n"
           "It receives the signed installer EXECUTE.KELF, res/info.sys,\n"
           "jacket images and the PFS boot system.cnf header, plus\n"
           "config/network.ini and the state/ journal directory that game\n"
           "installs need. Existing config/ and state/ are kept.\n\nContinue?",
           INSTALLER_PARTITION);
  if (!ui_confirm("Install Installer as XMB Channel", txt))
    return;
  ui_header("Install Installer as XMB Channel", "Working...");
  selfinstall_report_t rep;
  installer_app_install(&rep);
  if (rep.err) {
    snprintf(txt, sizeof(txt),
             "Failed: %s (%s)\nDriver code: %d\nStep: %s\n\n%s",
             err_text(rep.err), err_name(rep.err), rep.rc, rep.detail ? rep.detail : "-",
             rep.created ? "The partially created partition was removed."
                         : "An existing installer partition was left in place.");
    ui_message("Installer XMB App", txt);
    return;
  }
  snprintf(txt, sizeof(txt),
           "%s verified:\n  EXECUTE.KELF  (from %s)\n  res/info.sys\n"
           "  res/jkt_001.png, res/jkt_002.png\n  PPAA/system.cnf header\n"
           "  config/network.ini, state/\n\n"
           "Game installation is now enabled.\n"
           "The installer appears in the XMB after the XMB refreshes or the\n"
           "console reboots. The bootstrap ELF was not deleted.",
           INSTALLER_PARTITION, rep.kelf_origin ? rep.kelf_origin : "?");
  ui_message("Installer XMB App", txt);
}

