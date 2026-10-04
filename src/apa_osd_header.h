#ifndef PSXI_APA_OSD_HEADER_H
#define PSXI_APA_OSD_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Minimal OSD/XMB partition-header writer (plan section 18), ported
 * from hdl-dump's hdd_inject_header() system.cnf path.
 *
 * Offsets below are relative to the PPAA area, which starts at
 * partition-relative 0x1000. That is also offset 0 of a raw
 * `hdd0:<partition>` file handle: the APA driver maps fd offset 0 to
 * partition sector 8 (ps2sdk apa hdd_fio.c fioDataTransfer).
 *
 *   0x000  "PS2ICON3D" magic (9 bytes, no terminator)
 *   0x010  u32le system.cnf offset, relative to PPAA start (0x200)
 *   0x014  u32le system.cnf length
 *   0x200  system.cnf bytes (slot ends at 0x400)
 *
 * The writer only touches those three ranges; every other byte of the
 * region is preserved (read-modify-write).
 */

#define PPAA_MAGIC "PS2ICON3D"
#define PPAA_MAGIC_LEN 9
#define PPAA_SYSCNF_DESC 0x010
#define PPAA_SYSCNF_OFF 0x200
#define PPAA_SYSCNF_MAX 0x200
#define PPAA_REGION_LEN 0x400 /* 2 sectors: header + system.cnf slot */

/* Apply magic + system.cnf to a region buffer read from the device.
 * Bytes of the system.cnf slot past `len` are zeroed. Returns ERR_OK
 * or ERR_INVALID_ARG (len 0 or > PPAA_SYSCNF_MAX, region too small). */
inst_err_t ppaa_apply(uint8_t *region, size_t region_len, const char *syscnf,
                      size_t len);

/* Verify the region holds the magic, a descriptor pointing at 0x200
 * with `len`, and exactly the expected system.cnf bytes. ERR_OK or
 * ERR_XMB_VERIFY. */
inst_err_t ppaa_verify(const uint8_t *region, size_t region_len,
                       const char *syscnf, size_t len);

/* Extract the system.cnf text from a region (for diagnostics/repair
 * scans). Returns length copied, -1 if no valid header. */
int ppaa_read_syscnf(const uint8_t *region, size_t region_len, char *out,
                     size_t outsz);

#ifdef _EE
/* Read-modify-write the header of `hdd0:<partition>` and verify by
 * re-reading. ERR_OK, ERR_XMB_HEADER_WRITE or ERR_XMB_VERIFY.
 * `rc_out` receives the underlying driver code on failure. */
inst_err_t ppaa_write_partition(const char *partition, const char *syscnf,
                                size_t len, int *rc_out);

/* Raw-read and verify only. */
inst_err_t ppaa_verify_partition(const char *partition, const char *syscnf,
                                 size_t len, int *rc_out);
#endif

#endif
