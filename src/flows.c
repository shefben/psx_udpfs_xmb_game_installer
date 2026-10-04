#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "flows.h"
#include "hdd_partitions.h"
#include "hdl_install.h"
#include "network.h"
#include "opl_dependency.h"
#include "opl_launcher_payload.h"
#include "settings.h"
#include "transaction.h"
#include "ui.h"
#include "util.h"
#include "xmb_installer_app.h"

/* ------------------------------------------------------------------ */
/* Install progress screen                                             */

typedef struct {
  const game_plan_t *p;
  install_stage_t stage;
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
  (void)ctx;
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
  ui_at(ROW_BAR + 4, " Hold [SELECT]+[O] to abort (no XMB channel will be created).");
}

static int cb_abort(void *ctx) {
  (void)ctx;
  /* Two buttons so a stray press cannot abort a long copy. */
  return (ui_poll_button() & UI_CIRCLE) && (ui_held_buttons() & UI_SELECT);
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
  progress_ctx_t ctx = {p, STAGE_PREPARING};
  install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx};
  install_report_t rep;
  draw_install_static(p, "Installing");
  ui_footer("Do not power off.");
  game_install(p, allow_without_opl, &ui, &rep);

  if (rep.err == ERR_OK) {
    /* TX_COMPLETE reached: only now report success. */
    char msg[700];
    snprintf(msg, sizeof(msg),
             "%s installed (TX_COMPLETE).\n\n%s\n%s\n\n"
             "Bytes copied:     %llu\nBytes read back:  %llu\n"
             "Source CRC-32:    %08lx\nInstalled CRC-32: %08lx\n\n"
             "The game appears as its own XMB channel\n"
             "after the XMB refreshes (return to the XMB or reboot).",
             p->title, p->visible, p->hidden, (unsigned long long)rep.bytes_written,
             (unsigned long long)rep.bytes_verified, (unsigned long)rep.source_crc32,
             (unsigned long)rep.installed_crc32);
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
    if (opl_check_runtime(&opl, &rc) != ERR_OK) {
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
  progress_ctx_t ctx = {NULL, STAGE_VALIDATING};
  install_ui_t ui = {cb_stage, NULL, NULL, &ctx};
  install_report_t rep;
  ui_header("Create/Repair XMB Channel", hidden);
  game_create_channel(hidden, &ui, &rep);
  if (rep.err)
    flow_show_error("XMB channel was not created.", &rep, recovery_for(&rep));
  else
    ui_message("XMB channel created",
               "The channel was created and verified.\n"
               "It appears after the XMB refreshes. Game data was not rewritten.");
}

void flow_pair_actions(const char *visible, const char *hidden) {
  pair_facts_t f;
  game_pair_facts(visible, hidden, &f);
  pair_state_t st = pair_classify(&f);
  unsigned acts = pair_actions(st);

  static char rows[6][UI_ROW_LEN];
  int map[6], n = 0;
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

  char status[96];
  snprintf(status, sizeof(status), "State: %s", pair_state_label(st));
  ui_header("Existing installation", status);
  int c = ui_select("Existing installation", status, rows, n, 0, NULL, NULL);
  if (c < 0)
    return;
  switch (map[c]) {
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
/* Installed games / repair                                            */

#define MAX_PAIRS 128
typedef struct {
  char visible[APA_NAME_MAX + 1];
  char hidden[APA_NAME_MAX + 1];
  pair_state_t state;
} pair_row_t;

static pair_row_t pairs[MAX_PAIRS];
static char pair_rows[MAX_PAIRS][UI_ROW_LEN];
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
    snprintf(pair_rows[i], UI_ROW_LEN, "%-34.34s %s", pairs[i].visible + 3,
             pair_state_label(pairs[i].state));
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
    int c = ui_select(title, status, pair_rows, n, sel, NULL, NULL);
    if (c < 0)
      return;
    sel = c;
    flow_pair_actions(pairs[c].visible, pairs[c].hidden);
  }
}

void flow_installed_games(void) { pair_list("Installed Games", 0); }

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
  static char rows[4][UI_ROW_LEN];
  for (;;) {
    snprintf(rows[0], UI_ROW_LEN, "Local IP:  %s", g_app.settings.local_ip);
    snprintf(rows[1], UI_ROW_LEN, "Save and restart network");
    snprintf(rows[2], UI_ROW_LEN, "Restart network (retry discovery)");
    int c = ui_select("Network Settings", network_status_line(), rows, 3, 0, NULL, NULL);
    if (c < 0)
      return;
    if (c == 0) {
      char ip[16];
      str_copy(ip, g_app.settings.local_ip, sizeof(ip));
      int oct = 0;
      ui_header("Edit IP", "Static IP of this console (ministack has no DHCP)");
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
  if (!ui_confirm("Install/Repair Installer XMB App", txt))
    return;
  ui_header("Install/Repair Installer XMB App", "Working...");
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

