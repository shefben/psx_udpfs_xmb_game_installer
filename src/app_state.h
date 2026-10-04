#ifndef PSXI_APP_STATE_H
#define PSXI_APP_STATE_H

#include "iop_boot.h"
#include "settings.h"

/* Network states (plan section 8). The browser only runs in READY. */
typedef enum {
  NETWORK_DOWN = 0,
  NETWORK_STARTING,
  NETWORK_DISCOVERING,
  NETWORK_READY,
  NETWORK_ERROR,
} net_state_t;

typedef struct {
  iop_status_t iop;
  net_state_t net;
  net_settings_t settings;
  int app_mounted;    /* PP.UDPFS-INSTALLER mounted at pfs0: */
  int app_exists;     /* partition present on the HDD */
  int hdd_state;      /* inst_err_t from hdd_status() */
  int unfinished_txs; /* count found at startup */
} app_state_t;

extern app_state_t g_app;

const char *net_state_name(net_state_t s);

/* Paths inside the installer application partition. */
#define APP_CONFIG_DIR "pfs0:/config"
#define APP_NETWORK_INI "pfs0:/config/network.ini"
#define APP_STATE_DIR "pfs0:/state"
#define APP_PAYLOAD_DIR "pfs0:/payload"

/* Full IOP (re)boot: base modules, mount the installer partition (if
 * present), load network.ini, bring the network up. Re-opens the pad. */
void app_boot(void);

/* Mount/unmount the installer partition at pfs0:. */
void app_mount(void);
void app_unmount(void);

#endif
