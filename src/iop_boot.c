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
#include "rw_buffer.h"
#include "source_wire.h"

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
IRX(usbd);
IRX(bdm);
IRX(bdmfs_fatfs);
IRX(usbmass_bd);
IRX(hddpump);
IRX(mcman);
IRX(mcserv);

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
  int ok = ret >= 0 && (rv == 0 || rv == 2);
  if (st->nmods < IOP_MAX_MODS) {
    st->mods[st->nmods].module = name;
    st->mods[st->nmods].ok = ok;
    st->mods[st->nmods].ret = ret;
    st->mods[st->nmods].rv = rv;
    st->mods[st->nmods].data = data;
    st->mods[st->nmods].size = size;
    st->nmods++;
  }
  if (!ok) {
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
  /* Optional: overlapped installs (iop/hddpump). */
  st->pump_ok = st->hdd_ok && LOAD(st, hddpump, 0, NULL) == 0;

  int pad_fail = 0;
  pad_fail |= LOAD(st, sio2man, 0, NULL);
  pad_fail |= LOAD(st, padman, 0, NULL);
  st->pad_ok = !pad_fail;

  /* USB mass storage (FAT32/exFAT) as mass0: - an optional game source. */
  int usb_fail = 0;
  usb_fail |= LOAD(st, usbd, 0, NULL);
  usb_fail |= LOAD(st, bdm, 0, NULL);
  usb_fail |= LOAD(st, bdmfs_fatfs, 0, NULL);
  usb_fail |= LOAD(st, usbmass_bd, 0, NULL);
  st->usb_ok = !usb_fail;
}

/* ministack's DHCP result, written here by SIF DMA (layout: ministack
 * include/dhcp.h in patches/neutrino/0002-ministack-dhcp.patch). */
typedef struct {
  u32 magic, ip, server, status;
} dhcp_result_t;
#define DHCP_RESULT_MAGIC 0x50484344u
/* A whole cache line of its own: it is invalidated after the DMA. */
static union {
  dhcp_result_t r;
  u8 line[64];
} dhcp_buf __attribute__((aligned(64)));
#define dhcp_res dhcp_buf.r

int iop_load_memcard(iop_status_t *st) {
  static int loaded = -1;
  static int at_reboot = -1;
  extern int _iop_reboot_count;
  if (loaded >= 0 && at_reboot == _iop_reboot_count)
    return loaded;
  at_reboot = _iop_reboot_count;
  /* mcman needs sio2man, which iop_boot_base loaded for the pads. */
  loaded = LOAD(st, mcman, 0, NULL) == 0 && LOAD(st, mcserv, 0, NULL) == 0 ? 0 : -1;
  return loaded;
}

void iop_boot_network(const char *local_ip, int dhcp, iop_status_t *st) {
  st->net_ok = st->udpfs_ok = 0;
  st->dhcp_status = 0;
  st->ip = 0;
  /* Network: exactly one ip= argument to ministack. udpfs_ioman runs
   * server discovery (5 s) inside its device init; if that fails the
   * `udpfs:` device is simply not registered. */
  /* ministack args: "ip=<a.b.c.d>\0[dhcp=1\0out=<hex>\0]". */
  char ip_arg[64];
  int n = snprintf(ip_arg, sizeof(ip_arg), "ip=%s", local_ip);
  if (dhcp) {
    memset(&dhcp_buf, 0, sizeof(dhcp_buf));
    SyncDCache(&dhcp_buf, (u8 *)&dhcp_buf + sizeof(dhcp_buf) - 1);
    n++;
    n += snprintf(ip_arg + n, sizeof(ip_arg) - n, "dhcp=1") + 1;
    n += snprintf(ip_arg + n, sizeof(ip_arg) - n, "out=%x", (unsigned)&dhcp_res);
  }
  int net_fail = 0;
  net_fail |= LOAD(st, smap, 0, NULL);
  net_fail |= LOAD(st, ministack, n + 1, ip_arg);
  if (!net_fail && dhcp) {
    /* ministack's DHCP exchange (in its _start) also waited for the link. */
    InvalidDCache(&dhcp_buf, (u8 *)&dhcp_buf + sizeof(dhcp_buf) - 1);
    if (dhcp_res.magic == DHCP_RESULT_MAGIC) {
      st->dhcp_status = (int)dhcp_res.status;
      st->ip = dhcp_res.ip;
    } else {
      st->dhcp_status = 3;
    }
  } else if (!net_fail) {
    /* Give the PHY time to negotiate before discovery starts. */
    DelayThread(3 * 1000 * 1000);
  }
  net_fail |= LOAD(st, udpfs_ioman, 0, NULL);
  /* After the last module load, so the bigger buffer cannot starve one. */
  st->rw_buffer = rw_buffer_setup(fileXioSetRWBufferSize);
  if (st->rw_buffer)
    source_wire_set_request((uint32_t)st->rw_buffer);
  st->net_ok = !net_fail;
  if (st->net_ok) {
    int dd = fileXioDopen("udpfs:/");
    if (dd >= 0) {
      fileXioDclose(dd);
      st->udpfs_ok = 1;
    }
  }
}
