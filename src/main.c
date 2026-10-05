#include <kernel.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "batch.h"
#include "browser.h"
#include "diagnostics.h"
#include "flows.h"
#include "hdd_partitions.h"
#include "manifest.h"
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
                    "First run: " INSTALLER_PARTITION " does not exist yet.\n\n"
                    "Game installation stays disabled until it exists, because\n"
                    "it holds the install journals and network settings.\n"
                    "Run 'Install Installer as XMB Channel' first. Diagnostics\n"
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
  app_boot(); /* the network keeps starting in the background */
  startup_notices();

  static char rows[9][UI_ROW_LEN] = {
      "Install Games from UDPFS",
      "Install All Games from the server",
      "Installed Games",
      "Remove Games",
      "Repair XMB Channels",
      "Network Settings",
      "Install Installer as XMB Channel",
      "Diagnostics",
      "Exit",
  };
  int sel = 0, user_acted = 0, auto_ran = 0;
  for (;;) {
    int hdd_ok = g_app.iop.hdd_ok && g_app.hdd_state == ERR_OK;
    /* udpfsd.cfg auto_install = yes: once the server answers, install
     * without input - unless someone is already using the menu. */
    if (auto_start_due(hdd_ok, g_app.net == NETWORK_READY, g_manifest_loaded,
                       g_manifest_loaded && g_manifest.auto_install, user_acted, auto_ran)) {
      auto_ran = 1;
      flow_auto_install();
    }
    enum {
      M_BROWSE, M_BATCH, M_INSTALLED, M_REMOVE, M_REPAIR, M_NET, M_SELF, M_DIAG, M_EXIT,
      M_COUNT
    };
    /* Returns early when the network state changes, to redraw it. */
    int c = ui_select_live("Main menu", network_status_line(), rows, M_COUNT, sel,
                           "[Up/Down] move  [X] select");
    if (c < 0)
      continue;
    sel = c;
    user_acted = 1;
    int needs_games = c == M_BROWSE || c == M_BATCH || c == M_INSTALLED || c == M_REMOVE ||
                      c == M_REPAIR;
    if (!hdd_ok && (needs_games || c == M_SELF)) {
      ui_message("HDD unavailable",
                 "The internal HDD is not usable or a required HDD module failed\n"
                 "to load. All HDD writes are disabled. See Diagnostics.");
      continue;
    }
    if (!g_app.app_mounted && needs_games) {
      ui_message("Installer partition required",
                 INSTALLER_PARTITION " is not present (or could not be mounted).\n\n"
                 "It stores the install journals that prove a game was copied\n"
                 "and verified, so games cannot be installed or managed without\n"
                 "it. Choose 'Install Installer as XMB Channel' first.");
      continue;
    }
    switch (c) {
    case M_BROWSE:
      browser_run();
      break;
    case M_BATCH:
      flow_batch_install();
      break;
    case M_INSTALLED:
      flow_installed_games();
      break;
    case M_REMOVE:
      flow_remove_games();
      break;
    case M_REPAIR:
      flow_repair();
      break;
    case M_NET:
      flow_network_settings();
      break;
    case M_SELF:
      flow_self_install();
      break;
    case M_DIAG:
      flow_diagnostics();
      break;
    case M_EXIT:
      if (ui_confirm("Exit", "Return to the system menu?")) {
        network_wait_idle();
        app_unmount();
        ui_pad_close();
        LoadExecPS2("rom0:OSDSYS", 0, NULL);
      }
      break;
    }
  }
  return 0;
}
