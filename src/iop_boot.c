#include <delaythread.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <libpwroff.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#include "iop_boot.h"

/* Embedded IRX images (bin2c, see Makefile). */
#define IRX(n)                                                                 \
  extern unsigned char n##_irx[];                                              \
  extern unsigned int size_##n##_irx
IRX(iomanX);
IRX(fileXio);
IRX(poweroff);
IRX(ps2dev9);
IRX(ps2atad);
IRX(ps2hdd_hdl);
IRX(ps2fs);
IRX(hdlfs);
IRX(smap);
IRX(ministack);
IRX(udpfs_ioman);
IRX(sio2man);
IRX(padman);

/* ps2hdd-hdl.irx: -o 4 -n 128 (as proven by ps2-usbhdl/HDLGameInstaller). */
static const char PS2HDD_ARGS[] = "-o\0"
                                  "4\0"
                                  "-n\0"
                                  "128";
/* ps2fs.irx: two simultaneous mounts (pfs0: app, pfs1: work) plus slack. */
static const char PS2FS_ARGS[] = "-m\0"
                                 "4\0"
                                 "-o\0"
                                 "10\0"
                                 "-n\0"
                                 "40";

static int load(iop_status_t *st, const char *name, void *data,
                unsigned int size, int arglen, const char *args) {
  int rv = -1;
  int ret = SifExecModuleBuffer(data, size, arglen, args, &rv);
  /* rv: 0 RESIDENT_END, 2 REMOVABLE_END are fine; 1 NO_RESIDENT_END
   * means the module refused to stay (e.g. device init failed). */
  if (ret < 0 || (rv != 0 && rv != 2)) {
    if (st->nfails < IOP_MAX_FAILS) {
      st->fails[st->nfails].module = name;
      st->fails[st->nfails].ret = ret;
      st->fails[st->nfails].rv = rv;
    }
    st->nfails++;
    return -1;
  }
  return 0;
}

#define LOAD(st, n, alen, a) load(st, #n, n##_irx, size_##n##_irx, alen, a)

void iop_boot_base(iop_status_t *st) {
  memset(st, 0, sizeof(*st));

  SifInitRpc(0);
  while (!SifIopReset("", 0)) {
  }
  while (!SifIopSync()) {
  }
  SifInitRpc(0);
  SifInitIopHeap();
  SifLoadFileInit();
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();

  int hdd_fail = 0;
  hdd_fail |= LOAD(st, iomanX, 0, NULL);
  hdd_fail |= LOAD(st, fileXio, 0, NULL);
  fileXioInit();
  hdd_fail |= LOAD(st, poweroff, 0, NULL);
  poweroffInit();
  hdd_fail |= LOAD(st, ps2dev9, 0, NULL);
  hdd_fail |= LOAD(st, ps2atad, 0, NULL);
  hdd_fail |= LOAD(st, ps2hdd_hdl, sizeof(PS2HDD_ARGS), PS2HDD_ARGS);
  hdd_fail |= LOAD(st, ps2fs, sizeof(PS2FS_ARGS), PS2FS_ARGS);
  hdd_fail |= LOAD(st, hdlfs, 0, NULL);
  st->hdd_ok = !hdd_fail;

  int pad_fail = 0;
  pad_fail |= LOAD(st, sio2man, 0, NULL);
  pad_fail |= LOAD(st, padman, 0, NULL);
  st->pad_ok = !pad_fail;
}

void iop_boot_network(const char *local_ip, iop_status_t *st) {
  st->net_ok = st->udpfs_ok = 0;
  /* Network: exactly one ip= argument to ministack. udpfs_ioman runs
   * server discovery (5 s) inside its device init; if that fails the
   * `udpfs:` device is simply not registered. */
  char ip_arg[24];
  int n = snprintf(ip_arg, sizeof(ip_arg), "ip=%s", local_ip);
  int net_fail = 0;
  net_fail |= LOAD(st, smap, 0, NULL);
  net_fail |= LOAD(st, ministack, n + 1, ip_arg);
  if (!net_fail) {
    /* Give the PHY time to negotiate before discovery starts. */
    DelayThread(3 * 1000 * 1000);
  }
  net_fail |= LOAD(st, udpfs_ioman, 0, NULL);
  st->net_ok = !net_fail;
  if (st->net_ok) {
    int dd = fileXioDopen("udpfs:/");
    if (dd >= 0) {
      fileXioDclose(dd);
      st->udpfs_ok = 1;
    }
  }
}
