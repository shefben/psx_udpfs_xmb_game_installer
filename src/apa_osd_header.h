#ifndef PSXI_APA_OSD_HEADER_H
#define PSXI_APA_OSD_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* OSD/XMB partition-header ("PPAA" attribute area) writer, ported from
 * hdl-dump's hdd_inject_header()/modify_header and HDLGameInstaller's
 * InstallOSDFile(), which write the same layout.
 *
 * Offsets below are relative to the PPAA area, which starts at
 * partition-relative 0x1000. That is also offset 0 of a raw
 * `hdd0:<partition>` file handle: the APA driver maps fd offset 0 to
 * partition sector 8 (ps2sdk apa hdd_fio.c fioDataTransfer).
 *
 *   0x000  "PS2ICON3D" magic (9 bytes, no terminator)
 *   0x010  u32le system.cnf offset (0x200), u32le length
 *   0x018  u32le icon.sys offset (0x400), u32le length     (optional)
 *   0x020  u32le list icon offset (0x800), u32le length    (optional)
 *   0x028  u32le delete icon offset, u32le length: the list icon again
 *   0x200  system.cnf bytes (slot ends at 0x400)
 *   0x400  icon.sys bytes (slot ends at 0x800)
 *   0x800  icon bytes, zero-padded to a 512-byte boundary
 *
 * PFS-BatchKit-Manager, PSX-XMB-Manager (hdl_dump modify_header) and
 * HDLGameInstaller write all three files into every game partition,
 * the hidden HDL one included. Bytes outside the written ranges are
 * preserved (read-modify-write). Everything stays below 0x100000, where
 * an HDL partition keeps its game header.
 */

#define PPAA_MAGIC "PS2ICON3D"
#define PPAA_MAGIC_LEN 9
#define PPAA_SYSCNF_DESC 0x010
#define PPAA_SYSCNF_OFF 0x200
#define PPAA_SYSCNF_MAX 0x200
#define PPAA_ICONSYS_DESC 0x018
#define PPAA_ICONSYS_OFF 0x400
#define PPAA_ICONSYS_MAX 0x400
#define PPAA_ICON_DESC 0x020
#define PPAA_DELICON_DESC 0x028
#define PPAA_ICON_OFF 0x800
#define PPAA_ICON_MAX 0x10000 /* hdl_dump allows 0x3F800; ours is ~33 KiB */
#define PPAA_REGION_LEN 0x400 /* 2 sectors: header + system.cnf slot */
#define PPAA_OSD_MAX (PPAA_ICON_OFF + PPAA_ICON_MAX)

typedef struct {
  const char *syscnf;
  size_t syscnf_len;
  const char *iconsys; /* HDD-format "PS2X" icon.sys; NULL = none */
  size_t iconsys_len;
  const uint8_t *icon; /* list and delete icon; NULL = none */
  size_t icon_len;
} ppaa_files_t;

/* Bytes from the PPAA start that `f` covers (512-byte multiple), or 0
 * if a file is empty where required or too large. */
size_t ppaa_files_span(const ppaa_files_t *f);

/* Apply magic, descriptors and files to a region read from the device
 * (at least ppaa_files_span() bytes). Slot bytes past each file are
 * zeroed. ERR_OK or ERR_INVALID_ARG. */
inst_err_t ppaa_apply_files(uint8_t *region, size_t region_len, const ppaa_files_t *f);

/* Region holds exactly these files and descriptors. ERR_OK or
 * ERR_XMB_VERIFY. */
inst_err_t ppaa_verify_files(const uint8_t *region, size_t region_len, const ppaa_files_t *f);

/* system.cnf only (icon.sys and icon descriptors untouched). */
inst_err_t ppaa_apply(uint8_t *region, size_t region_len, const char *syscnf,
                      size_t len);
inst_err_t ppaa_verify(const uint8_t *region, size_t region_len,
                       const char *syscnf, size_t len);

/* The header names an icon.sys and a list icon (any content). */
int ppaa_has_icons(const uint8_t *region, size_t region_len);

/* Extract the system.cnf text from a region (for diagnostics/repair
 * scans). Returns length copied, -1 if no valid header. */
int ppaa_read_syscnf(const uint8_t *region, size_t region_len, char *out,
                     size_t outsz);

#ifdef _EE
/* Read-modify-write the header of `hdd0:<partition>` and verify by
 * re-reading. ERR_OK, ERR_XMB_HEADER_WRITE or ERR_XMB_VERIFY.
 * `rc_out` receives the underlying driver code on failure. */
inst_err_t ppaa_write_files(const char *partition, const ppaa_files_t *f, int *rc_out);

/* Raw-read and verify only. */
inst_err_t ppaa_verify_partition_files(const char *partition, const ppaa_files_t *f,
                                       int *rc_out);

/* Quick check: magic, this exact system.cnf, and icon.sys + icon
 * present. */
inst_err_t ppaa_check_partition(const char *partition, const char *syscnf, size_t len,
                                int *rc_out);
#endif

#endif
