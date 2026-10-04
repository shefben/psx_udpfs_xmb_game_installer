#ifndef PSXI_HDL_HEADER_H
#define PSXI_HDL_HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "iso9660.h"

/* HDLFS on-disk structures, binary-compatible with HDLGameInstaller
 * hdlfs/hdlfs.h (ec37c81). Do not change these layouts. */

#define HDLFS_GAME_TITLE_LEN 160
#define HDLFS_STARTUP_PTH_LEN 60
#define HDL_INFO_MAGIC 0xDEADFEEDu
#define HDL_GAME_DATA_OFFSET 0x100000 /* within the raw hdd0: fd */
#define HDL_HEADER_SIZE 1024
#define HDL_MAX_PART_SPECS 65

struct HDLFS_FormatArgs {
  uint8_t CompatFlags, DiscType;
  uint8_t TRType, TRMode;
  uint32_t NumSectors, Layer1Start;
  char GameTitle[HDLFS_GAME_TITLE_LEN];      /* UTF-8 */
  char StartupPath[HDLFS_STARTUP_PTH_LEN];   /* no ";1" suffix */
};

_Static_assert(sizeof(struct HDLFS_FormatArgs) == 232, "HDLFS_FormatArgs size");
_Static_assert(offsetof(struct HDLFS_FormatArgs, DiscType) == 1, "DiscType");
_Static_assert(offsetof(struct HDLFS_FormatArgs, NumSectors) == 4, "NumSectors");
_Static_assert(offsetof(struct HDLFS_FormatArgs, Layer1Start) == 8, "Layer1Start");
_Static_assert(offsetof(struct HDLFS_FormatArgs, GameTitle) == 12, "GameTitle");
_Static_assert(offsetof(struct HDLFS_FormatArgs, StartupPath) == 172, "StartupPath");

/* Fill format args from probe results and the chosen display title. */
void hdl_format_args_build(struct HDLFS_FormatArgs *a, const iso_info_t *iso,
                           const char *title);

/* Installer completion marker, kept in hdl_game_info.reserved (u16 at
 * header offset 4). hdlfs writes 0 there; OPL, OPL-Launcher and
 * hdl-dump never read it and OPL's header edit preserves it. The
 * installer writes INCOMPLETE right after format and COMPLETE only
 * after the copied data verified, so an interrupted copy stays
 * recognisable even if its journal is lost. */
#define HDL_MARK_NONE 0
#define HDL_MARK_INCOMPLETE 0x4955 /* "UI" */
#define HDL_MARK_COMPLETE 0x4355   /* "UC" */
#define HDL_MARK_OFFSET 4

/* Set the marker in a raw header buffer (bytes 4..5 only). */
void hdl_header_set_marker(uint8_t *buf, uint16_t marker);

/* Parsed hdl_game_info (offsets per hdlfs.h / hdl-dump hdl.c). */
typedef struct {
  uint32_t magic;
  int marker; /* HDL_MARK_*; unknown values read as HDL_MARK_NONE */
  uint16_t version;
  char title[HDLFS_GAME_TITLE_LEN + 1];
  char startup[HDLFS_STARTUP_PTH_LEN + 1];
  uint32_t layer1_start;
  uint32_t disc_type;
  int num_partitions;
  uint64_t data_bytes; /* sum of part_specs[].part_size */
} hdl_header_info_t;

/* Parse a 1024-byte header read from hdd0 fd offset 0x100000.
 * Returns 0 if the magic is right and num_partitions is in range,
 * -1 otherwise (out is still filled best-effort). */
int hdl_header_parse(const uint8_t buf[HDL_HEADER_SIZE], hdl_header_info_t *out);

/* Header checks for verification: magic, startup == boot_id, non-empty
 * title, disc type, layer break, partition count, exact data size. */
inst_err_t hdl_header_check(const hdl_header_info_t *h, const char *boot_id,
                            uint8_t disc_type, uint32_t layer1_start,
                            int expected_parts, uint64_t expected_bytes);

/* ---- payload sanity ---- */

/* A signed KELF is never a plain ELF and has at least a header. */
int kelf_looks_valid(const void *data, uint32_t size);

/* PNG signature, an IHDR first chunk, 1..1024 px each side, and a
 * total size limit; enough to reject non-PNG and absurd files. */
int png_basic_valid(const void *data, uint32_t size);

/* ---- display title ---- */

/* Default display title: the PVD volume id unless it is empty or just
 * the disc id (common on PS2 discs), else the file name without
 * directories and ".iso"/".zso.iso". */
void default_display_title(const iso_info_t *iso, const char *source_path,
                           char *out, size_t outsz);

#endif
