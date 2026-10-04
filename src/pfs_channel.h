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
} channel_content_t;

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
 * valid PPAA/system.cnf header. ERR_OK or ERR_XMB_VERIFY. */
inst_err_t channel_quick_check(const char *partition);

/* Create (128 MiB), populate and verify. On any failure the visible
 * partition is removed again so no broken channel is left behind. */
channel_result_t channel_create(const char *partition,
                                const channel_content_t *c);

#endif
