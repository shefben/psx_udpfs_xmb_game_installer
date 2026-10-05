#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "hdd_partitions.h"
#include "hdl_install.h"
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
  const char *name = INSTALLER_PARTITION;
  g_app.app_rename_rc = 0;
  if (g_app.iop.hdd_ok) {
    /* Earlier releases' PP.UDPFS-INSTALLER was never listed by the XMB;
     * give it the XMB-shaped name. If that fails, keep using it as is
     * so its journals and settings are not lost. */
    g_app.app_rename_rc = installer_partition_migrate();
    if (g_app.app_rename_rc < 0)
      name = INSTALLER_LEGACY_NAME;
  }
  g_app.app_exists = g_app.iop.hdd_ok && hdd_exists(name) > 0;
  if (g_app.app_exists && pfs_mount(PFS_APP, name, FIO_MT_RDWR) == 0)
    g_app.app_mounted = 1;
}

void app_boot(void) {
  network_wait_idle(); /* never reset the IOP under the network thread */
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

  g_hdl_use_pump = g_app.iop.pump_ok && g_app.settings.fast_copy;
  ui_pad_open();
  network_start(); /* background: the menu does not wait for udpfsd */
}
