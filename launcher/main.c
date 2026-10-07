/*
 * App launcher: the EXECUTE.KELF of the app channels UDPFS Game Installer
 * makes ("PP.APPS-NNNNN..TITLE", src/apps.h). One signed program for
 * every app, because the console cannot sign the app's own ELF.
 *
 * The XMB starts it with argv[0] = "hdd0:<partition>:<path>"; a homebrew
 * loader may pass the partition as argv[1] instead. This is the boot
 * sequence of OPL-Launcher (reference/OPL-Launcher/src/main.c), which
 * starts from PSX XMB channels: load the HDD modules, mount the own
 * partition as pfs0:, then elf-loader runs the app's ELF from there
 * (it resets the IOP before the app starts). The ELF and its arguments
 * come from pfs0:APP.CFG (src/app_cfg.h).
 */
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

#include <elf-loader.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_cfg.h"

#define IRX(n)                                                                 \
  extern unsigned char n##_irx[];                                              \
  extern unsigned int size_##n##_irx
IRX(ps2dev9);
IRX(iomanX);
IRX(fileXio);
IRX(ps2atad);
IRX(ps2hdd);
IRX(ps2fs);

#define LOAD(n) SifExecModuleBuffer(n##_irx, size_##n##_irx, 0, NULL, NULL)

static void boot_error(void) {
  SifExitRpc();
  char *args[2] = {"BootError", NULL};
  ExecOSD(1, args);
}

/* "hdd0:<partition>:<path>" -> partition (as OPL-Launcher's
 * GetMountParams: everything between the device and the next ':'). */
static int partition_from_argv0(const char *a, char *out, size_t outsz) {
  if (!a || strlen(a) < 6 || a[4] != ':')
    return -1;
  const char *b = a + 5, *e = strchr(b, ':');
  if (!e || e == b || (size_t)(e - b) >= outsz)
    return -1;
  memcpy(out, b, (size_t)(e - b));
  out[e - b] = 0;
  return 0;
}

int main(int argc, char *argv[]) {
  char part[33];
  SifInitRpc(0);
  if (argc > 1) {
    /* Started by a homebrew loader: clear its IOP modules first. */
    while (!SifIopReset(NULL, 0)) {
    }
    int ok = strlen(argv[1]) < sizeof(part);
    if (ok)
      strcpy(part, argv[1]);
    while (!SifIopSync()) {
    }
    SifInitRpc(0);
    if (!ok)
      boot_error();
  } else if (argc < 1 || partition_from_argv0(argv[0], part, sizeof(part)) < 0) {
    boot_error();
  }

  SifInitIopHeap();
  SifLoadFileInit();
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();
  LOAD(ps2dev9);
  LOAD(iomanX);
  LOAD(fileXio);
  fileXioInit();
  LOAD(ps2atad);
  LOAD(ps2hdd);
  LOAD(ps2fs);
  SifLoadFileExit();
  SifExitIopHeap();

  char dev[40];
  snprintf(dev, sizeof(dev), "hdd0:%s", part);
  fileXioUmount("pfs0:");
  if (fileXioMount("pfs0:", dev, FIO_MT_RDONLY) < 0)
    boot_error();

  static char text[2048];
  int n = -1, fd = fileXioOpen("pfs0:" APP_CFG_FILE, FIO_O_RDONLY);
  if (fd >= 0) {
    n = fileXioRead(fd, text, sizeof(text));
    fileXioClose(fd);
  }
  static app_cfg_t cfg;
  if (n <= 0 || app_cfg_parse(text, (size_t)n, &cfg) < 0)
    boot_error();

  static char path[8 + APP_PATH_MAX];
  snprintf(path, sizeof(path), "pfs0:%s", cfg.boot);
  char *args[APP_ARGS_MAX];
  for (int i = 0; i < cfg.nargs; i++)
    args[i] = cfg.args[i];
  LoadELFFromFile(path, cfg.nargs, args); /* returns only on failure */
  boot_error();
  return 1;
}
