#ifndef PSXI_OPL_LAUNCHER_PAYLOAD_H
#define PSXI_OPL_LAUNCHER_PAYLOAD_H

#include <stdint.h>

#include "errors.h"

/* Signed KELF payloads and embedded jacket images (plan 15, 20-22).
 *
 * KELFs are never generated on the console. Lookup order:
 *   OPL-Launcher:  embedded at build time (vendor/opl-launcher/EXECUTE.KELF)
 *                  -> pfs0:/payload/OPL-LAUNCHER.KELF (installer partition)
 *                  -> udpfs:/PAYLOAD/opl-launcher-EXECUTE.KELF
 *   Installer:     udpfs:/PAYLOAD/installer-EXECUTE.KELF
 *                  -> pfs0:/EXECUTE.KELF (an existing installer channel)
 * Every candidate must pass kelf_looks_valid().
 */

typedef struct {
  const uint8_t *data;
  uint32_t size;
  int owned;          /* 1 if malloc'd (payload_release frees) */
  const char *origin; /* "embedded", a path, ... */
} payload_t;

inst_err_t payload_opl_launcher(payload_t *out, int app_mounted, int udpfs_ok);
inst_err_t payload_installer(payload_t *out, int app_mounted, int udpfs_ok);
void payload_release(payload_t *p);

/* 1 if an OPL-Launcher KELF was embedded at build time. */
int payload_opl_launcher_embedded(void);

/* Embedded PNGs (always present). */
void payload_default_jacket(const uint8_t **data, uint32_t *size);
void payload_installer_jacket(const uint8_t **data, uint32_t *size);

#endif
