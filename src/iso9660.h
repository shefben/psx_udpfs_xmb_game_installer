#ifndef PSXI_ISO9660_H
#define PSXI_ISO9660_H

#include <stdint.h>

#include "errors.h"
#include "source.h"

#define ISO_SECTOR 2048u

/* HDLFS DiscType values (must match HDLFS_FormatArgs, plan 10). */
#define DISC_TYPE_CD 0x12
#define DISC_TYPE_DVD 0x14

/* Hint from the server folder layout (OPL convention /CD/, /DVD/). */
typedef enum { DISC_HINT_NONE = 0, DISC_HINT_CD, DISC_HINT_DVD } disc_hint_t;

typedef struct {
  char volume_id[33];   /* PVD volume identifier, trailing spaces trimmed */
  char boot2[64];       /* raw BOOT2 value from SYSTEM.CNF */
  char boot_id[16];     /* "SLUS_203.12" */
  char part_id[11];     /* "SLUS-20312" */
  uint64_t source_size; /* logical bytes of the source (whole file) */
  uint32_t pvd_blocks;  /* PVD volume space size, 2 KiB blocks (layer 0) */
  uint32_t sectors;     /* 2 KiB sectors to install = source_size / 2048 */
  uint8_t disc_type;    /* DISC_TYPE_CD or DISC_TYPE_DVD */
  uint32_t layer1_start; /* 0 unless DVD9 */
  int has_udf;          /* UDF volume recognition sequence present */
} iso_info_t;

/* Probe a PS2 disc image through `src` (already open). Validates the
 * PVD ("CD001"), locates SYSTEM.CNF in the root directory, extracts
 * BOOT2 and normalizes the startup id, sizes the image and decides
 * disc type and DVD9 layer break. Never infers identity from a file
 * name. Returns ERR_OK, ERR_SOURCE_READ, ERR_SOURCE_INVALID_ISO or
 * ERR_SOURCE_SYSTEM_CNF. */
inst_err_t iso_probe(GameSource *src, disc_hint_t hint, iso_info_t *out);

/* Folder hint from a source path: a path component equal to "CD" or
 * "DVD" (case-insensitive); the nearest such component wins. */
disc_hint_t iso_hint_from_path(const char *path);

#endif
