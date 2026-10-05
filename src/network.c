#include <kernel.h>
#include <stdio.h>

#include "app_state.h"
#include "manifest.h"
#include "network.h"
#include "ui.h"

/* The network half of the IOP boot (smap, ministack, udpfs_ioman with
 * its ~5 s server discovery, plus a 3 s PHY wait) runs on its own EE
 * thread so the menu is usable at once. Shared resources:
 *   - fileXio: its EE RPC client serialises callers with a semaphore;
 *   - SifExecModuleBuffer: only this thread loads modules meanwhile;
 *   - libpad and the screen: only the main thread uses them;
 *   - g_manifest: written here before NETWORK_READY is published, and
 *     read by the menus only once the state is NETWORK_READY.
 * The thread runs at a higher priority than main; it spends nearly all
 * its time blocked in RPCs or DelayThread, so the menu stays responsive. */

#define NET_THREAD_PRIO 32
#define MAIN_THREAD_PRIO 64

extern void *_gp;
static u8 net_stack[64 * 1024] __attribute__((aligned(16)));
static int net_thread = -1;
static volatile int net_busy;

static void net_job(void) {
  iop_boot_network(g_app.settings.local_ip, &g_app.iop);
  net_state_t st = !g_app.iop.net_ok ? NETWORK_ERROR
                   : g_app.iop.udpfs_ok ? NETWORK_READY
                                        : NETWORK_ERROR;
  if (st == NETWORK_READY)
    manifest_load(); /* udpfsd's prepared game list, if it offers one */
  else
    g_manifest_loaded = 0;
  g_app.net = st; /* published last: readers see a loaded manifest */
  net_busy = 0;
  ui_wake();
}

static void net_thread_main(void *arg) {
  (void)arg;
  net_job();
  ExitThread();
}

void network_start(void) {
  network_wait_idle();
  if (net_thread >= 0) {
    DeleteThread(net_thread); /* dormant since its ExitThread */
    net_thread = -1;
  }
  g_manifest_loaded = 0;
  net_busy = 1;
  g_app.net = NETWORK_DISCOVERING;
  ui_wake();

  ChangeThreadPriority(GetThreadId(), MAIN_THREAD_PRIO);
  ee_thread_t t = {0};
  t.func = (void *)net_thread_main;
  t.stack = net_stack;
  t.stack_size = sizeof(net_stack);
  t.gp_reg = &_gp;
  t.initial_priority = NET_THREAD_PRIO;
  int id = CreateThread(&t);
  if (id >= 0 && StartThread(id, NULL) >= 0) {
    net_thread = id;
    return;
  }
  if (id >= 0)
    DeleteThread(id);
  /* No thread: start the network in the foreground as before. */
  ui_header("Starting", "Starting network (static IP)...");
  ui_at(4, " Looking for udpfsd on the LAN (up to ~8 s)...");
  net_job();
}

int network_busy(void) { return net_busy; }

void network_wait_idle(void) {
  if (!net_busy)
    return;
  ui_header("Please wait", "Network start-up is still running...");
  ui_at(4, " Waiting for the udpfsd search to finish (up to ~8 s).");
  while (net_busy)
    ui_delay_ms(50);
}

void network_restart(void) { app_boot(); }

const char *network_not_ready_text(void) {
  return net_busy ? "Still looking for udpfsd on the network (see the status line\n"
                    "at the top of the main menu). Try again in a few seconds."
                  : "udpfsd was not found on the network.\n\n"
                    "Check that udpfsd is running on the PC, that UDP port 62966\n"
                    "is allowed through its firewall, and the DESR IP in\n"
                    "Network Settings. Then use Network Settings > Restart.";
}

const char *network_status_line(void) {
  static char line[96];
  const char *why = "";
  if (g_app.net == NETWORK_ERROR)
    why = g_app.iop.net_ok ? " (udpfsd not found)" : " (network modules failed)";
  else if (g_app.net == NETWORK_DISCOVERING)
    why = " (looking for udpfsd...)";
  snprintf(line, sizeof(line), "%s %s%s", net_state_name(g_app.net),
           g_app.settings.local_ip, why);
  return line;
}
