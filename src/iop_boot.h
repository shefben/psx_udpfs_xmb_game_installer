#ifndef PSXI_IOP_BOOT_H
#define PSXI_IOP_BOOT_H

#include "errors.h"

/* The one authoritative IOP boot sequence (plan section 7).
 *
 * It runs in two halves so the static IP can come from network.ini,
 * which lives on the HDD:
 *
 *   iop_boot_base():    reset/sync IOP, RPC, sbv patches, iomanX,
 *                       fileXio, poweroff, ps2dev9, ps2atad,
 *                       ps2hdd-hdl, ps2fs, hdlfs, sio2man, padman,
 *                       usbd, bdm, bdmfs_fatfs, usbmass_bd
 *   iop_boot_network(): smap, ministack ip=<ip>, udpfs_ioman, then the
 *                       64 KiB fileXio transfer buffer (rw_buffer.h)
 *
 * The module order is exactly the plan's; the controller modules are
 * loaded with the base half ("if not already available") so the UI can
 * run while the network half is starting. Changing the IP needs both
 * halves again (ministack cannot be reconfigured once resident).
 */

#define IOP_MAX_FAILS 16
#define IOP_MAX_MODS 20

typedef struct {
  /* Every load attempt, in order (diagnostics; `data`/`size` is the
   * embedded image, so its SHA-256 can be shown). */
  int nmods;
  struct {
    const char *module;
    int ok, ret, rv;
    const void *data;
    unsigned int size;
  } mods[IOP_MAX_MODS];
  int hdd_ok;   /* iomanX..hdlfs all loaded: HDD writes allowed */
  int pad_ok;   /* sio2man + padman loaded */
  int usb_ok;   /* usbd + bdm + bdmfs_fatfs + usbmass_bd loaded (mass0:) */
  int net_ok;   /* smap + ministack + udpfs_ioman loaded */
  int udpfs_ok; /* udpfs: device registered (server discovered) */
  int rw_buffer; /* fileXio IOP transfer buffer in bytes (rw_buffer.h) */
  int nfails;
  struct {
    const char *module;
    int ret; /* SifExecModuleBuffer return */
    int rv;  /* module _start result */
  } fails[IOP_MAX_FAILS];
} iop_status_t;

void iop_boot_base(iop_status_t *st);
void iop_boot_network(const char *local_ip, iop_status_t *st);

#endif
