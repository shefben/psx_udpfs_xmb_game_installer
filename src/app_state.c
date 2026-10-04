#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "hdd_partitions.h"
#include "network.h"
#include "ui.h"

app_state_t g_app;

const char *net_state_name(net_state_t s) {
  switch (s) {
  case NETWORK_DOWN:
    return "NETWORK_DOWN";
  case NETWORK_STARTING:
    return "NETWORK_STARTING";
  case NETWORK_DISCOVERING:
    return "NETWORK_DISCOVERING";
  case NETWORK_READY:
    return "NETWORK_READY";
  case NETWORK_ERROR:
    return "NETWORK_ERROR";
  }
  return "?";
}

void app_unmount(void) {
  if (g_app.app_mounted)
    pfs_umount(PFS_APP);
  g_app.app_mounted = 0;
}

void app_mount(void) {
  app_unmount();
  g_app.app_exists = g_app.iop.hdd_ok && hdd_exists(INSTALLER_PARTITION) > 0;
  if (g_app.app_exists && pfs_mount(PFS_APP, INSTALLER_PARTITION, FIO_MT_RDWR) == 0)
    g_app.app_mounted = 1;
}

void app_boot(void) {
  ui_pad_close();
  app_unmount();
  g_app.net = NETWORK_DOWN;

  ui_header("Starting", "Loading IOP modules...");
  iop_boot_base(&g_app.iop);
  g_app.hdd_state = g_app.iop.hdd_ok ? hdd_status() : ERR_HDD_MISSING;
  if (g_app.hdd_state == ERR_OK)
    app_mount();

  if (g_app.app_mounted)
    settings_load(APP_NETWORK_INI, &g_app.settings);
  else if (!g_app.settings.local_ip[0])
    settings_parse(NULL, &g_app.settings);

  ui_pad_open();
  network_start();
}
