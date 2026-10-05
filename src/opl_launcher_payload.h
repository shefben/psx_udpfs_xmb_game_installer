#ifndef PSXI_OPL_LAUNCHER_PAYLOAD_H
#define PSXI_OPL_LAUNCHER_PAYLOAD_H

#include <stdint.h>

#include "errors.h"

/* Signed KELF payloads and embedded jacket images (plan 15, 20-22).
 *
 * Build variants (see Makefile):
 *   app        desr-udpfs-installer-app.elf, signed as the XMB channel's
 *              EXECUTE.KELF. Embeds the signed OPL-Launcher KELF.
 *   bootstrap  desr-udpfs-installer-bootstrap.elf, run once from a
 *              homebrew launcher. Embeds the signed OPL-Launcher KELF
 *              AND the signed app KELF it installs into
 *              PP.UDPF-00001..INSTALLER.
 *   dev        unsigned development build (make dev). Nothing embedded;
 *              payloads may come from udpfs:/PAYLOAD/ as an explicit
 *              development fallback. Never shipped in dist/.
 *
 * KELFs are never generated on the console. Every candidate must pass
 * kelf_looks_valid().
 */

typedef struct {
  const uint8_t *data;
  uint32_t size;
  int owned;          /* 1 if malloc'd (payload_release frees) */
  const char *origin; /* "embedded", a path, ... */
} payload_t;

/* OPL-Launcher KELF for game channels: embedded copy; in dev builds
 * only, udpfs:/PAYLOAD/opl-launcher-EXECUTE.KELF. */
inst_err_t payload_opl_launcher(payload_t *out, int udpfs_ok);

/* Installer app KELF for PP.UDPF-00001..INSTALLER: bootstrap -> embedded;
 * app -> its own pfs0:/EXECUTE.KELF (repair in place); dev ->
 * udpfs:/PAYLOAD/installer-EXECUTE.KELF. */
inst_err_t payload_installer(payload_t *out, int app_mounted, int udpfs_ok);

void payload_release(payload_t *p);

/* "app", "bootstrap" or "dev". */
const char *payload_build_variant(void);
int payload_opl_launcher_embedded(void);
int payload_installer_embedded(void);

/* res/jkt_001.png (large, 140x200) and res/jkt_002.png (XMB list cover,
 * 74x108), the sizes PSX-XMB-Manager writes for DESR channels. */
#define JKT_LARGE_W 140
#define JKT_LARGE_H 200
#define JKT_SMALL_W 74
#define JKT_SMALL_H 108
typedef struct {
  const void *large;
  uint32_t large_size;
  const void *small;
  uint32_t small_size;
} jacket_pair_t;

/* Embedded PNGs (always present). */
void payload_default_jackets(jacket_pair_t *j);
void payload_installer_jackets(jacket_pair_t *j);

/* Blank 640x350 page for the default res/man.xml (image/0..2.png). */
void payload_manual_page(const uint8_t **data, uint32_t *size);

/* HDLGameInstaller's default OSD icon (res/icon.ico), written as the
 * list and delete icon of every partition header we create. */
void payload_osd_icon(const uint8_t **data, uint32_t *size);

#endif
