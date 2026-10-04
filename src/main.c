#include <kernel.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "browser.h"
#include "diagnostics.h"
#include "flows.h"
#include "network.h"
#include "transaction.h"
#include "ui.h"

static void startup_notices(void) {
  char msg[600];
  int off = 0;
  if (g_app.hdd_state != ERR_OK)
    off += snprintf(msg + off, sizeof(msg) - off,
                    "Internal HDD: %s. Installing is disabled.\n\n",
                    err_text((inst_err_t)g_app.hdd_state));
  if (!g_app.iop.hdd_ok)
    off += snprintf(msg + off, sizeof(msg) - off,
                    "A required HDD module failed to load (see Diagnostics).\n"
                    "No HDD writes will be made.\n\n");
  if (g_app.settings.warning)
    off += snprintf(msg + off, sizeof(msg) - off,
                    "config/network.ini is invalid; using default IP %s.\n\n",
                    SETTINGS_DEFAULT_IP);
  if (g_app.app_mounted) {
    static tx_journal_t txs[16];
    int n = tx_scan_unfinished(APP_STATE_DIR, txs, 16);
    g_app.unfinished_txs = n;
    if (n > 0)
      off += snprintf(msg + off, sizeof(msg) - off,
                      "%d unfinished install(s) found. No incomplete game is\n"
                      "shown in the XMB. See Repair XMB Channels.\n\n", n);
  } else if (g_app.hdd_state == ERR_OK) {
    off += snprintf(msg + off, sizeof(msg) - off,
                    "First run: PP.UDPFS-INSTALLER does not exist yet.\n\n"
                    "Game installation stays disabled until it exists, because\n"
                    "it holds the install journals and network settings.\n"
                    "Run 'Install/Repair Installer XMB App' first. Diagnostics\n"
                    "are available now.\n\n");
  }
  if (off > 0)
    ui_message("Notice", msg);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  ui_init();
  memset(&g_app, 0, sizeof(g_app));
  app_boot();
  startup_notices();

  static char rows[7][UI_ROW_LEN] = {
      "Install Games from UDPFS",
      "Installed Games",
      "Repair XMB Channels",
      "Network Settings",
      "Install/Repair Installer XMB App",
      "Diagnostics",
      "Exit",
  };
  int sel = 0;
  for (;;) {
    int hdd_ok = g_app.iop.hdd_ok && g_app.hdd_state == ERR_OK;
    int c = ui_select("Main menu", network_status_line(), rows, 7, sel,
                      "[Up/Down] move  [X] select", NULL);
    if (c < 0)
      continue;
    sel = c;
    if (!hdd_ok && (c == 0 || c == 1 || c == 2 || c == 4)) {
      ui_message("HDD unavailable",
                 "The internal HDD is not usable or a required HDD module failed\n"
                 "to load. All HDD writes are disabled. See Diagnostics.");
      continue;
    }
    if (!g_app.app_mounted && (c == 0 || c == 1 || c == 2)) {
      ui_message("Installer partition required",
                 "PP.UDPFS-INSTALLER is not present (or could not be mounted).\n\n"
                 "It stores the install journals that prove a game was copied\n"
                 "and verified, so games cannot be installed or managed without\n"
                 "it. Choose 'Install/Repair Installer XMB App' first.");
      continue;
    }
    switch (c) {
    case 0:
      browser_run();
      break;
    case 1:
      flow_installed_games();
      break;
    case 2:
      flow_repair();
      break;
    case 3:
      flow_network_settings();
      break;
    case 4:
      flow_self_install();
      break;
    case 5:
      flow_diagnostics();
      break;
    case 6:
      if (ui_confirm("Exit", "Return to the system menu?")) {
        app_unmount();
        ui_pad_close();
        LoadExecPS2("rom0:OSDSYS", 0, NULL);
      }
      break;
    }
  }
  return 0;
}
