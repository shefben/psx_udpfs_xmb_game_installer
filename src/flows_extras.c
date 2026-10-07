#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "extras.h"
#include "extras_install.h"
#include "flows.h"
#include "hdd_partitions.h"
#include "partname.h"
#include "pops_install.h"
#include "ui.h"
#include "util.h"

/* Saves, Cheats & Game Extras: puts the server's (or the USB drive's)
 * VMC, CHT, CFG and ART files of installed games where OPL and
 * POPStarter read them (extras_install.h). */

#define XG_MAX 128
typedef struct {
  char part[APA_NAME_MAX + 1]; /* PP.SLUS-20312..TITLE */
  char id[16];                 /* SLUS_203.12 */
  int ps1;
} xgame_t;

static xgame_t g_games[XG_MAX];
static int g_ngames;
static hdd_part_t g_parts[256];
static char g_rows[XG_MAX][UI_ROW_LEN];

static int has_partner_hidden(const char *visible, int np) {
  char h[APA_NAME_MAX + 1];
  if (partition_partner(visible, h) < 0)
    return 0;
  for (int i = 0; i < np; i++)
    if (!strcmp(g_parts[i].name, h))
      return 1;
  return 0;
}

/* Installed games shown in the XMB: PS2 (HDL partition, or an older
 * release's PFS channel with its __. data) and PS1 (PFS with IMAGE0.VCD). */
static int collect_games(void) {
  ui_header("Scanning", "Reading APA partition table...");
  int np = hdd_list(g_parts, 256);
  g_ngames = 0;
  for (int i = 0; i < np && g_ngames < XG_MAX; i++) {
    const hdd_part_t *p = &g_parts[i];
    if (!partition_is_game_channel(p->name))
      continue;
    xgame_t *g = &g_games[g_ngames];
    if (part_id_from_partition(p->name, g->id) < 0)
      continue;
    str_copy(g->part, p->name, sizeof(g->part));
    if (p->type == APA_TYPE_HDL_ID)
      g->ps1 = 0;
    else if (p->type == APA_TYPE_PFS_ID && has_partner_hidden(p->name, np))
      g->ps1 = 0;
    else if (p->type == APA_TYPE_PFS_ID && pops_partition_is_ps1(p->name))
      g->ps1 = 1;
    else
      continue;
    g_ngames++;
  }
  return np < 0 ? np : g_ngames;
}

static void game_row(const xgame_t *g, char *out) {
  snprintf(out, UI_ROW_LEN, "[%s] %-60.60s", g->ps1 ? "PS1" : "PS2", g->part + 3);
}

/* Pick an installed game; only PS1 or PS2 ones if kind_ps1 >= 0, the one
 * with `prefer_id` first. Index into g_games, or -1. */
static int pick_game(const char *title, int kind_ps1, const char *prefer_id) {
  static int map[XG_MAX];
  int n = 0, start = 0;
  for (int i = 0; i < g_ngames; i++) {
    if (kind_ps1 >= 0 && g_games[i].ps1 != kind_ps1)
      continue;
    if (prefer_id && prefer_id[0] && !strcmp(g_games[i].id, prefer_id))
      start = n;
    map[n] = i;
    game_row(&g_games[i], g_rows[n]);
    n++;
  }
  if (!n) {
    ui_message(title, kind_ps1 == 1   ? "No PS1 game is installed."
                      : kind_ps1 == 0 ? "No PS2 game is installed."
                                      : "No game is installed.");
    return -1;
  }
  int c = ui_select(title, NULL, g_rows, n, start, "[X] select  [O] back", NULL);
  return c < 0 ? -1 : map[c];
}

static void report_text(char *out, size_t sz, const xgame_t *g, const extras_report_t *r) {
  if (!r->found)
    snprintf(out, sz, "--        %s\n", g->part + 3);
  else
    snprintf(out, sz, "%-9s %s\n          %.62s%s%.50s\n",
             r->failed ? "PROBLEM" : r->installed ? "INSTALLED" : "KEPT", g->part + 3,
             r->installed ? r->what : "nothing new", r->note[0] ? " - " : "", r->note);
}

static int ask_opts(extras_opts_t *o) {
  o->replace_cfg = ui_confirm("Game settings",
                              "Replace OPL settings that are already on the HDD with the\n"
                              "server's CFG files?\n\n"
                              "[X] replace  [O] keep the ones on the HDD (cheat and memory\n"
                              "card entries are still added to them).");
  o->replace_cards = ui_confirm_destructive(
      "Memory cards",
      "Replace memory cards the games ALREADY have with the server's?\n\n"
      "The saves on those cards are LOST. Without this, only games\n"
      "without a card get one.");
  return 0;
}

static void install_for(int all) {
  char root[16];
  if (extras_source_root(root, sizeof(root)) < 0) {
    ui_message("Game extras", "No server and no USB drive: nothing to install from.");
    return;
  }
  if (collect_games() <= 0) {
    ui_message("Game extras", "No installed games found.");
    return;
  }
  int one = -1;
  if (!all && (one = pick_game("Game extras for", -1, NULL)) < 0)
    return;
  extras_opts_t o;
  ask_opts(&o);
  static char text[8192];
  size_t t = 0;
  int found = 0, failed = 0;
  t += (size_t)snprintf(text, sizeof(text), "From %s (VMC, CHT, CFG, ART folders)\n\n",
                        !strcmp(root, "udpfs:") ? "the server" : "the USB drive");
  for (int i = 0; i < g_ngames; i++) {
    if (!all && i != one)
      continue;
    const xgame_t *g = &g_games[i];
    ui_header("Installing game extras", g->part + 3);
    ui_at(4, " %d / %d", i + 1, g_ngames);
    extras_report_t r;
    if (g->ps1)
      extras_install_ps1(g->part, g->id, &o, &r);
    else
      extras_install_ps2(g->id, &o, &r);
    found += r.found > 0;
    failed += r.failed > 0;
    if (t < sizeof(text))
      report_text(text + t, sizeof(text) - t, g, &r);
    t = strlen(text);
  }
  char head[120];
  snprintf(head, sizeof(head), "\n%d game%s had extras, %d with problems.", found,
           found == 1 ? "" : "s", failed);
  if (t + strlen(head) < sizeof(text))
    strcat(text, head);
  ui_text_view("Game extras", text);
}

/* ---- single files ---------------------------------------------------- */

#define XF_MAX 128
static char g_files[XF_MAX][64];

static int list_vmc(const char *root) {
  char dir[24];
  snprintf(dir, sizeof(dir), "%s/VMC", root);
  int n = 0, dd = fileXioDopen(dir);
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  while (n < XF_MAX && fileXioDread(dd, &de) > 0) {
    if (de.name[0] == '.' || (de.stat.mode & FIO_S_IFMT) == FIO_S_IFDIR ||
        strlen(de.name) >= sizeof(g_files[0]))
      continue;
    extra_kind_t k = extra_classify(de.name);
    if (k == EXTRA_PS2_VMC || k == EXTRA_PS2_SAVE || k == EXTRA_PS1_CARD || k == EXTRA_PS1_SAVE)
      str_copy(g_files[n++], de.name, sizeof(g_files[0]));
  }
  fileXioDclose(dd);
  return n;
}

static int pick_slot(const char *title, const char *a, const char *b) {
  static char rows[2][UI_ROW_LEN];
  str_copy(rows[0], a, UI_ROW_LEN);
  str_copy(rows[1], b, UI_ROW_LEN);
  return ui_select(title, NULL, rows, 2, 0, "[X] select  [O] back", NULL);
}

static void one_file(const char *root, const char *name) {
  char src[160], id[16] = "";
  int slot = 0;
  snprintf(src, sizeof(src), "%s/VMC/%s", root, name);
  extra_name_id(name, id, &slot);
  extra_kind_t k = extra_classify(name);
  const char *why = NULL;
  inst_err_t e;
  int replace = 0;
  if (k == EXTRA_PS2_SAVE) {
    int port = pick_slot("Copy the save to", "Memory card slot 1", "Memory card slot 2");
    if (port < 0)
      return;
    for (;;) {
      ui_header("Copying save", name);
      e = extras_psu_to_card(src, port, replace, &why);
      if (e != ERR_PARTITION_EXISTS || replace)
        break;
      if (!ui_confirm_destructive("Replace save?",
                                  "This save is already on the memory card.\n"
                                  "Overwrite its files with the server's?"))
        return;
      replace = 1;
    }
  } else {
    int ps1 = k == EXTRA_PS1_CARD || k == EXTRA_PS1_SAVE;
    if (collect_games() < 0) {
      ui_message("Memory cards", "Cannot read the HDD partition table.");
      return;
    }
    int g = pick_game(ps1 ? "PS1 game for this card" : "PS2 game for this card", ps1, id);
    if (g < 0)
      return;
    int s = pick_slot("Memory card slot", ps1 ? "Slot 1 (SLOT0.VMC)" : "Slot 1 ($VMC_0)",
                      ps1 ? "Slot 2 (SLOT1.VMC)" : "Slot 2 ($VMC_1)");
    if (s < 0)
      return;
    for (;;) {
      ui_header("Installing", name);
      if (k == EXTRA_PS2_VMC)
        e = extras_ps2_card_to_game(src, g_games[g].id, s, replace, &why);
      else if (k == EXTRA_PS1_CARD)
        e = extras_ps1_card_to_game(src, g_games[g].part, s, replace, &why);
      else
        e = extras_ps1_save_to_game(src, g_games[g].part, s, replace, &why);
      if (e != ERR_PARTITION_EXISTS || replace)
        break;
      if (!ui_confirm_destructive(
              "Replace?", k == EXTRA_PS1_SAVE
                              ? "A save with this name is already on the game's card.\n"
                                "Replace it (the other saves on the card stay)?"
                              : "The game already has a memory card in that slot.\n"
                                "Replace it? The saves on it are LOST."))
        return;
      replace = 1;
    }
  }
  char msg[300];
  if (e)
    snprintf(msg, sizeof(msg), "%s\n\nNot installed: %s (%s).", name, why ? why : err_text(e),
             err_name(e));
  else
    snprintf(msg, sizeof(msg), "%s\n\nInstalled and read back.", name);
  ui_message(e ? "Not installed" : "Done", msg);
}

static void browse_files(void) {
  char root[16];
  if (extras_source_root(root, sizeof(root)) < 0) {
    ui_message("Memory cards & saves", "No server and no USB drive.");
    return;
  }
  int sel = 0;
  for (;;) {
    int n = list_vmc(root);
    if (n <= 0) {
      ui_message("Memory cards & saves",
                 "No memory cards or saves in the VMC folder.\n\n"
                 "PS2: OPL cards <GAME-ID>_0.bin / _1.bin (or any .bin), PCSX2 .ps2,\n"
                 "     saves .psu (copied onto a real memory card)\n"
                 "PS1: cards <GAME-ID>.VMC / .mcr / .mcd / .gme / .vmp,\n"
                 "     single saves .mcs");
      return;
    }
    for (int i = 0; i < n; i++)
      snprintf(g_rows[i], UI_ROW_LEN, "[%-10s] %.58s", extra_kind_label(extra_classify(g_files[i])),
               g_files[i]);
    int c = ui_select("Memory cards & saves", root, g_rows, n, sel, "[X] install  [O] back", NULL);
    if (c < 0)
      return;
    sel = c;
    one_file(root, g_files[c]);
  }
}

void flow_extras(void) {
  static char rows[4][UI_ROW_LEN] = {
      "Install extras for ALL installed games",
      "Install extras for one game",
      "Memory cards & saves (VMC folder, one file)",
      "Back",
  };
  int sel = 0;
  for (;;) {
    int c = ui_select("Saves, Cheats & Game Extras",
                      "VMC, CHT, CFG, ART folders on the server or USB", rows, 4, sel,
                      "[X] select  [O] back", NULL);
    if (c < 0 || c == 3)
      return;
    sel = c;
    if (c == 0)
      install_for(1);
    else if (c == 1)
      install_for(0);
    else
      browse_files();
  }
}
