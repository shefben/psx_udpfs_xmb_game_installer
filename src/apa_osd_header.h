#ifndef PSXI_APA_OSD_HEADER_H
#define PSXI_APA_OSD_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* OSD/XMB partition-header ("PPAA" attribute area) writer, ported from
 * hdl-dump's modify_header and HDLGameInstaller's InstallOSDFile(),
 * which write the same layout.
 *
 * Offsets below are relative to the PPAA area, which starts at
 * partition-relative 0x1000. That is also offset 0 of a raw
 * `hdd0:<partition>` file handle: the APA driver maps fd offset 0 to
 * partition sector 8 (ps2sdk apa hdd_fio.c fioDataTransfer).
 *
 *   0x000    "PS2ICON3D" magic (9 bytes, no terminator)
 *   0x010    u32le system.cnf offset (0x200), u32le length
 *   0x018    u32le icon.sys offset (0x400), u32le length     (optional)
 *   0x020    u32le list icon offset (0x800), u32le length    (optional)
 *   0x028    u32le delete icon offset, u32le length
 *   0x030    u32le boot KELF offset (0x110000), u32le length (optional)
 *   0x200    system.cnf bytes (slot ends at 0x400)
 *   0x400    icon.sys bytes (slot ends at 0x800)
 *   0x800    list icon, zero-padded to a 512-byte boundary
 *   0x40000  delete icon (del_copy: the list icon again, hdl_dump layout)
 *   0x100000 HDL game header of an HDL partition: never written here
 *   0x110000 boot KELF, started by "BOOT2 = PATINFO"
 *
 * A PS2 game the DESR XMB lists is, as PFS-BatchKit-Manager installs
 * it, ONE visible HDL partition "PP.<ID>..<TITLE>" whose header has
 * system.cnf "BOOT2 = PATINFO", icon.sys, list + delete icon and
 * OPL-Launcher as boot KELF. Bytes outside the written ranges are
 * preserved (read-modify-write of the first range).
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
#define PPAA_DELICON_OFF 0x40000
#define PPAA_ICON_MAX 0x10000 /* hdl_dump allows 0x3F800; ours is ~33 KiB */
#define PPAA_KELF_DESC 0x030
#define PPAA_KELF_OFF 0x110000
#define PPAA_KELF_MAX 0x1F0000 /* hdl_dump's boot.kelf limit */
#define PPAA_REGION_LEN 0x400  /* 2 sectors: header + system.cnf slot */
#define PPAA_OSD_MAX (PPAA_DELICON_OFF + PPAA_ICON_MAX)

typedef struct {
  const char *syscnf;
  size_t syscnf_len;
  const char *iconsys; /* HDD-format "PS2X" icon.sys; NULL = none */
  size_t iconsys_len;
  const uint8_t *icon; /* list icon; NULL = none */
  size_t icon_len;
  int del_copy;        /* delete icon: 1 = a copy at 0x40000, 0 = the list icon */
  const uint8_t *kelf; /* boot KELF at 0x110000; NULL = descriptor untouched */
  size_t kelf_len;
} ppaa_files_t;

/* Bytes from the PPAA start that the first range of `f` covers (512-byte
 * multiple; the KELF is written separately), or 0 if a file is empty
 * where required or too large. */
size_t ppaa_files_span(const ppaa_files_t *f);

/* Apply magic, descriptors and files (not the KELF bytes) to a region
 * read from the device (at least ppaa_files_span() bytes). Slot bytes
 * past each file are zeroed. ERR_OK or ERR_INVALID_ARG. */
inst_err_t ppaa_apply_files(uint8_t *region, size_t region_len, const ppaa_files_t *f);

/* Region holds exactly these files and descriptors (KELF: descriptor
 * only). ERR_OK or ERR_XMB_VERIFY. */
inst_err_t ppaa_verify_files(const uint8_t *region, size_t region_len, const ppaa_files_t *f);

/* system.cnf only (icon.sys and icon descriptors untouched). */
inst_err_t ppaa_apply(uint8_t *region, size_t region_len, const char *syscnf,
                      size_t len);
inst_err_t ppaa_verify(const uint8_t *region, size_t region_len,
                       const char *syscnf, size_t len);

/* The header names an icon.sys and a list icon (any content). */
int ppaa_has_icons(const uint8_t *region, size_t region_len);

/* The header names a boot KELF at 0x110000 of a plausible size. */
int ppaa_has_kelf(const uint8_t *region, size_t region_len);

/* Extract the system.cnf text from a region (for diagnostics/repair
 * scans). Returns length copied, -1 if no valid header. */
int ppaa_read_syscnf(const uint8_t *region, size_t region_len, char *out,
                     size_t outsz);

/* Copy the icon.sys text (NUL-terminated). Returns length, -1 if none. */
int ppaa_read_iconsys(const uint8_t *region, size_t region_len, char *out, size_t outsz);

#ifdef _EE
/* Read-modify-write the header of `hdd0:<partition>` (and write the
 * KELF, if any) and verify by re-reading. ERR_OK, ERR_XMB_HEADER_WRITE
 * or ERR_XMB_VERIFY. `rc_out` receives the driver code on failure. */
inst_err_t ppaa_write_files(const char *partition, const ppaa_files_t *f, int *rc_out);

/* Raw-read and verify only (KELF bytes included). */
inst_err_t ppaa_verify_partition_files(const char *partition, const ppaa_files_t *f,
                                       int *rc_out);

/* Quick check: magic, this exact system.cnf, icon.sys + icon present,
 * and a boot KELF if `need_kelf`. */
inst_err_t ppaa_check_partition(const char *partition, const char *syscnf, size_t len,
                                int need_kelf, int *rc_out);

/* icon.sys text of a partition's header. Returns length, -1 if none. */
int ppaa_partition_iconsys(const char *partition, char *out, size_t outsz);
#endif

#endif
