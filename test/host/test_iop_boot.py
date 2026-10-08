#!/usr/bin/env python3
"""Exercise the real boot sequence with a simulated resident IOP module list."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK_HEADERS = ["delaythread.h", "iopcontrol.h", "iopheap.h", "kernel.h",
               "libpwroff.h", "loadfile.h", "sbv_patches.h", "sifrpc.h",
               "fileXio_rpc.h", "smod.h"]
MOCK_SDK = r"""
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct { unsigned short id, version; } smod_mod_info_t;
int smod_get_mod_by_name(const char *, smod_mod_info_t *);
int SifExecModuleBuffer(void *, unsigned int, int, const char *, int *);
void SifInitRpc(int);
int SifIopReset(const char *, int);
int SifIopSync(void);
void SifInitIopHeap(void);
void SifLoadFileInit(void);
void sbv_patch_enable_lmb(void);
void sbv_patch_disable_prefix_check(void);
void fileXioInit(void);
void poweroffInit(void);
void SyncDCache(void *, void *);
void InvalidDCache(void *, void *);
void DelayThread(int);
int fileXioSetRWBufferSize(int);
int fileXioDopen(const char *);
int fileXioDclose(int);
"""
HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "iop_boot.h"
#define BLOB(n) unsigned char n##_irx[1]; unsigned int size_##n##_irx = 1;
BLOB(iomanX) BLOB(fileXio) BLOB(poweroff) BLOB(ps2dev9) BLOB(ps2atad)
BLOB(ps2hdd_hdl) BLOB(ps2fs) BLOB(hdlfs) BLOB(smap) BLOB(ministack)
BLOB(udpfs_ioman) BLOB(sio2man) BLOB(padman) BLOB(usbd) BLOB(bdm)
BLOB(bdmfs_fatfs) BLOB(usbmass_bd) BLOB(hddpump) BLOB(mcman) BLOB(mcserv)
int _iop_reboot_count;
static int mc_loads;
void source_wire_set_request(uint32_t n) { (void)n; }
static int existing_pad, existing_sio, fail_sio, pad_loads, sio_loads;
static int storage_loads, reset_done, queries;
int smod_get_mod_by_name(const char *name, smod_mod_info_t *info) {
  assert(reset_done); /* A pre-reset module list must never decide reuse. */
  queries++;
  info->id = 42;
  info->version = 0x0306;
  if (!strcmp(name, "padman")) return existing_pad ? 42 : 0;
  if (!strcmp(name, "sio2man")) return existing_sio ? 42 : 0;
  return 0;
}
int SifExecModuleBuffer(void *data, unsigned int size, int alen,
                        const char *args, int *rv) {
  (void)size; (void)alen; (void)args;
  *rv = 0;
  if (data == mcman_irx || data == mcserv_irx) mc_loads++;
  if (data == sio2man_irx) {
    sio_loads++;
    assert(!existing_pad); /* Do not disturb a working pad subsystem. */
    assert(!existing_sio);
    if (fail_sio) { *rv = 1; return 42; }
    existing_sio = 1;
  }
  if (data == padman_irx) {
    pad_loads++;
    assert(!existing_pad);
    assert(existing_sio); /* PADMAN depends on successful SIO2MAN init. */
    existing_pad = 1;
  }
  if (data == ps2atad_irx || data == ps2hdd_hdl_irx || data == ps2fs_irx)
    storage_loads++;
  return 42;
}
void SifInitRpc(int n) { (void)n; }
int SifIopReset(const char *arg, int n) { (void)arg; (void)n; return 1; }
int SifIopSync(void) { reset_done = 1; return 1; }
void SifInitIopHeap(void) {}
void SifLoadFileInit(void) {}
void sbv_patch_enable_lmb(void) {}
void sbv_patch_disable_prefix_check(void) {}
void fileXioInit(void) {}
void poweroffInit(void) {}
void SyncDCache(void *a, void *b) { (void)a; (void)b; }
void InvalidDCache(void *a, void *b) { (void)a; (void)b; }
void DelayThread(int n) { (void)n; }
int fileXioSetRWBufferSize(int n) { return n; }
int rw_buffer_setup(int (*set_size)(int)) { (void)set_size; return 0; }
int fileXioDopen(const char *p) { (void)p; return -1; }
int fileXioDclose(int fd) { (void)fd; return 0; }
int main(int argc, char **argv) {
  assert(argc == 2);
  existing_pad = !strcmp(argv[1], "resident-pad");
  existing_sio = !strcmp(argv[1], "resident-sio");
  fail_sio = !strcmp(argv[1], "failed-sio");
  iop_status_t st;
  iop_boot_base(&st);
  assert(queries > 0);
  assert(storage_loads == 3 && st.hdd_ok && st.usb_ok);
  if (!strcmp(argv[1], "resident-pad")) {
    assert(sio_loads == 0 && pad_loads == 0 && st.pad_ok);
    assert(st.pad_reused && st.pad_version == 0x0306);
    assert(!st.sio2_reused);
  } else if (!strcmp(argv[1], "resident-sio")) {
    assert(sio_loads == 0 && pad_loads == 1 && st.pad_ok);
    assert(st.sio2_reused && st.sio2_version == 0x0306);
    assert(!st.pad_reused);
  } else if (fail_sio) {
    assert(sio_loads == 1 && pad_loads == 0 && !st.pad_ok);
    assert(st.nfails == 1);
  } else {
    assert(sio_loads == 1 && pad_loads == 1 && st.pad_ok);
    assert(!st.pad_reused && !st.sio2_reused);
  }
  assert(mc_loads == 0); /* Memory card drivers are lazy. */
  if (!fail_sio) {
    assert(iop_load_memcard(&st) == 0 && mc_loads == 2);
    assert(iop_load_memcard(&st) == 0 && mc_loads == 2);
    _iop_reboot_count++;
    assert(iop_load_memcard(&st) == 0 && mc_loads == 4);
  }
  puts(argv[1]);
  return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="psxi-iop-boot-") as tmp:
        tmp = Path(tmp)
        for name in SDK_HEADERS:
            (tmp / name).write_text("/* supplied by forced mock SDK header */\n")
        (tmp / "mock_sdk.h").write_text(MOCK_SDK)
        (tmp / "harness.c").write_text(HARNESS)
        binary = tmp / "test_iop_boot"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall",
                        "-Wextra", "-Werror", "-Wno-pointer-to-int-cast", # EE is 32-bit.
                        "-I", str(tmp), "-I", str(ROOT / "src"),
                        "-include", str(tmp / "mock_sdk.h"),
                        str(ROOT / "src/iop_boot.c"), str(tmp / "harness.c"),
                        "-o", str(binary)], check=True)
        for scenario in ("resident-pad", "resident-sio", "fresh", "failed-sio"):
            subprocess.run([str(binary), scenario], check=True)


if __name__ == "__main__":
    main()
