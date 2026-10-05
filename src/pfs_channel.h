#ifndef PSXI_PFS_CHANNEL_H
#define PSXI_PFS_CHANNEL_H

#include <stdint.h>

#include "errors.h"

/* Visible PFS XMB channel partition (plan sections 16-18, 21). Used
 * for both per-game channels and the installer's own app channel. */

typedef struct {
  const void *kelf;
  uint32_t kelf_size;
  const char *info_sys;
  uint32_t info_sys_len;
  const void *jacket; /* PNG copied to both jkt_001 and jkt_002 */
  uint32_t jacket_size;
  const char *osd_title0; /* header icon.sys title0 (the XMB title) */
  const char *osd_title1; /* title1 (game ID or app ID) */
} channel_content_t;

/* Write the whole OSD header (PS2ICON3D + system.cnf + icon.sys + the
 * default list/delete icon) into `partition`, read it back, verify.
 * Every partition PFS-BatchKit-Manager / PSX-XMB-Manager create for the
 * XMB has all three; channels with system.cnf only froze the PSX XMB
 * once two of them existed. */
inst_err_t osd_header_write(const char *partition, const char *syscnf, const char *title0,
                            const char *title1, int *rc_out);
inst_err_t osd_header_verify(const char *partition, const char *syscnf, const char *title0,
                             const char *title1, int *rc_out);

typedef struct {
  inst_err_t err;
  int rc;
  const char *step; /* short label of the failing step */
} channel_result_t;

/* Write all channel files into an already-created, formatted PFS
 * partition, then inject the PPAA/system.cnf header. Does not remove
 * the partition on failure (caller decides). */
channel_result_t channel_populate(const char *partition,
                                  const channel_content_t *c);

/* Remount and compare every file byte-for-byte, then verify the
 * PPAA header and that it says BOOT2 = pfs:/EXECUTE.KELF. */
channel_result_t channel_verify(const char *partition,
                                const channel_content_t *c);

/* Lighter check for scans: files present with non-zero size and a
 * valid PPAA header (system.cnf, icon.sys and icon present). A channel
 * from an older build (system.cnf only) fails, so Installed Games
 * offers Repair XMB channel. ERR_OK or ERR_XMB_VERIFY. */
inst_err_t channel_quick_check(const char *partition);

/* Current XMB title of a channel (res/info.sys), or -1. */
int channel_get_title(const char *partition, char *out, size_t outsz);

/* Replace only the title line of res/info.sys (written as .tmp, read
 * back, renamed). Partition names and game data are not touched. */
channel_result_t channel_set_title(const char *partition, const char *title);

/* Create (128 MiB), populate and verify. On any failure the visible
 * partition is removed again so no broken channel is left behind. */
channel_result_t channel_create(const char *partition,
                                const channel_content_t *c);

#endif
