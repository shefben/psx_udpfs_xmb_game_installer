#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "errors.h"
#include "flows.h"
#include "hdd_health.h"
#include "hdd_partitions.h"
#include "pump.h"
#include "source_cdvd.h"
#include "ui.h"
#include "util.h"
#include "xmb_game_channel.h"

/* ------------------------------------------------------------------ */
/* Install a PS2 game from the console's own disc drive                */

void flow_disc_install(void) {
  ui_header("Install Game from Disc", "Reading the disc drive...");
  ui_at(4, " Insert a PS2 game disc (CD or DVD) into the drive.");
  ui_at(5, " Waiting for the drive to recognise it...");
  cdvd_disc_t d = cdvd_disc_detect(15000);
  const char *why = NULL;
  switch (d) {
  case CDVD_DISC_PS2_CD:
  case CDVD_DISC_PS2_DVD:
    break;
  case CDVD_DISC_NO_DRIVE:
    why = "The disc drive does not answer (cdvdfsv is not running).";
    break;
  case CDVD_DISC_NONE:
    why = "No disc found. Insert a PS2 game disc and try again.";
    break;
  case CDVD_DISC_PS1:
    why = "This is a PlayStation (PS1) disc. Copy it on a PC (BIN/CUE) and\n"
          "put it in the server's POPS folder: udpfsd serves it as a .VCD.";
    break;
  default:
    why = "This is not a PS2 game disc (video DVD, audio CD or unknown).";
    break;
  }
  if (why) {
    ui_message("Install Game from Disc", why);
    return;
  }
  game_plan_t plan;
  int rc = 0;
  ui_header("Checking", "Disc in the drive");
  ui_at(4, " Reading ISO9660 volume descriptor and SYSTEM.CNF...");
  inst_err_t e = game_plan_build(CDVD_PATH, &plan, &rc);
  if (e) {
    char msg[400];
    snprintf(msg, sizeof(msg),
             "%s (%s)\nDriver code: %d\n\n%s\n\nNothing was written to the HDD.", err_text(e),
             err_name(e), rc,
             e == ERR_SOURCE_INVALID_ISO || e == ERR_SOURCE_SYSTEM_CNF
                 ? "Not a PS2 game disc the installer can read (no PVD or SYSTEM.CNF)."
             : rc == CDVD_ERR_LAYERS ? "The disc's two layers could not be matched up (layer 1 is not\n"
                           "where layer 0 ends): it is not copied, so no half game ends up\n"
                           "on the HDD."
                         : "The disc could not be read, or the drive did not say whether\n"
                           "it has one or two layers: clean it and try again.");
    ui_message("Cannot install", msg);
    return;
  }
  flow_install_game(&plan);
  if (ui_confirm("Install Game from Disc", "Open the disc tray?"))
    cdvd_eject();
}

/* ------------------------------------------------------------------ */
/* HDD health check                                                    */

static pump_smart_t g_smart;
static smart_attr_t g_attr[SMART_MAX_ATTR];

static void health_text(char *out, size_t outsz, int detail) {
  size_t r = 0;
#define ADD(...)                                                                                \
  do {                                                                                          \
    if (r < outsz)                                                                              \
      r += (size_t)snprintf(out + r, outsz - r, __VA_ARGS__);                                   \
  } while (0)
  int n = -1;
  int have = pump_smart(&g_smart) == 0;
  if (have && g_smart.data_rc == 0)
    n = smart_parse(g_smart.data, g_attr, SMART_MAX_ATTR);
  health_t h = smart_verdict(have ? g_smart.status : -19, g_attr, n);
  ADD("SMART health:  %s\n", health_label(h));
  if (have && g_smart.status < 0 && n < 0)
    ADD("  (the DVRP does not pass SMART commands through: code %d)\n", g_smart.status);
  for (int i = 0; i < n; i++) {
    const char *name = smart_attr_name(g_attr[i].id);
    if (!name && !detail)
      continue;
    char label[40];
    if (name)
      str_copy(label, name, sizeof(label));
    else
      snprintf(label, sizeof(label), "Attribute %u", g_attr[i].id);
    ADD("  %3u %-24s %10llu   value %3u worst %3u\n", g_attr[i].id, label,
        (unsigned long long)smart_attr_display(&g_attr[i]), g_attr[i].value, g_attr[i].worst);
  }

  uint32_t total = 0, free_mb = 0, max_mb = 0;
  space_usage_t u;
  uint32_t hdd_free = 0;
  if (hdd_space_mb(&total, &free_mb, &max_mb) == 0 && hdd_usage(&u, &hdd_free) == 0) {
    ADD("\nSpace\n");
    ADD("  HDD %lu MiB, free %lu MiB, largest partition %lu MiB\n", (unsigned long)total,
        (unsigned long)free_mb, (unsigned long)max_mb);
    ADD("  Games %llu MiB, games+data %llu MiB, system %llu MiB\n",
        (unsigned long long)u.games_mb, (unsigned long long)u.data_mb,
        (unsigned long long)u.system_mb);
    uint64_t all = hdd_largest_game(free_mb, max_mb);
    uint32_t safe_budget = u.limit_left_mb < free_mb ? (uint32_t)u.limit_left_mb : free_mb;
    uint64_t safe = hdd_largest_game(safe_budget, max_mb);
    ADD("  Largest game that still fits: %llu MiB", (unsigned long long)(all >> 20));
    if (safe != all)
      ADD(" (%llu MiB within the 128 GiB safe limit)", (unsigned long long)(safe >> 20));
    ADD("\n  (a DVD-5 game needs up to 4482 MiB, a DVD-9 up to 8152 MiB)\n");
  } else {
    ADD("\nSpace: HDD usage unavailable\n");
  }
  uint32_t mfree = 0, mmax = 0;
  if (detail && pump_meminfo(&mfree, &mmax) == 0)
    ADD("\nIOP memory free %lu KiB (largest block %lu KiB)\n", (unsigned long)(mfree >> 10),
        (unsigned long)(mmax >> 10));
#undef ADD
}

void flow_hdd_health(void) {
  static char text[4096];
  static char rows[4][UI_ROW_LEN] = {
      "Check all installed games (read every game back)",
      "Show all SMART attributes",
      "Back",
  };
  for (;;) {
    ui_header("HDD Health Check", "Reading SMART and the partition table...");
    health_text(text, sizeof(text), 0);
    ui_text_view("HDD Health Check", text);
    int c = ui_select("HDD Health Check", NULL, rows, 3, 0, "[X] select  [O] back", NULL);
    if (c == 0) {
      flow_check_all_games();
    } else if (c == 1) {
      ui_header("HDD Health Check", "Reading SMART...");
      health_text(text, sizeof(text), 1);
      ui_text_view("SMART attributes", text);
    } else {
      return;
    }
  }
}
