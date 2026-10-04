#include <stdio.h>

#include "app_state.h"
#include "network.h"
#include "ui.h"

void network_start(void) {
  g_app.net = NETWORK_STARTING;
  ui_header("Starting", "Starting network (static IP)...");
  ui_at(4, " Local IP: %s%s", g_app.settings.local_ip,
        g_app.settings.using_default ? "  (default)" : "");
  if (g_app.settings.warning)
    ui_at(5, " WARNING: config/network.ini invalid - using default IP.");
  ui_at(7, " Looking for udpfsd on the LAN (up to ~8 s)...");
  g_app.net = NETWORK_DISCOVERING;
  iop_boot_network(g_app.settings.local_ip, &g_app.iop);
  if (!g_app.iop.net_ok)
    g_app.net = NETWORK_ERROR;
  else
    g_app.net = g_app.iop.udpfs_ok ? NETWORK_READY : NETWORK_ERROR;
}

void network_restart(void) { app_boot(); }

const char *network_status_line(void) {
  static char line[96];
  const char *why = "";
  if (g_app.net == NETWORK_ERROR)
    why = g_app.iop.net_ok ? " (udpfsd not found)" : " (network modules failed)";
  snprintf(line, sizeof(line), "%s %s%s", net_state_name(g_app.net),
           g_app.settings.local_ip, why);
  return line;
}
