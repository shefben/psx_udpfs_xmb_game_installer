#include <string.h>

#include "iso9660.h"
#include "partname.h"
#include "util.h"

/* Logic adapted from ps2-usbhdl src/iso.c (b681bc6) and OPL's
 * sbGetISO9660MaxLBA/sbProbeISO9660 DVD9 detection, moved behind the
 * 64-bit GameSource API. */

/* Largest CD image we accept as CD without a hint (99-minute CD). */
#define CD_MAX_SECTORS 445500u

#define PVD_LBA 16

typedef struct {
  uint32_t lba;
  uint32_t size;
} extent_t;

static uint8_t sector[ISO_SECTOR];

static inst_err_t read_sector(GameSource *src, uint32_t lba, void *buf) {
  return source_read_at(src, (uint64_t)lba * ISO_SECTOR, buf, ISO_SECTOR);
}

/* Find `target` (e.g. "SYSTEM.CNF") in a directory; matches with or
 * without a ";N" version suffix. 1 found, 0 not found, <0 error code
 * (negated inst_err_t). */
static int find_in_dir(GameSource *src, extent_t dir, const char *target,
                       extent_t *out) {
  size_t tlen = strlen(target);
  uint32_t left = dir.size, lba = dir.lba;
  /* Directories on PS2 discs are small; cap work on corrupt input. */
  if (left > 64 * ISO_SECTOR)
    left = 64 * ISO_SECTOR;

  while (left > 0) {
    inst_err_t e = read_sector(src, lba, sector);
    if (e)
      return -(int)e;
    uint32_t chunk = left < ISO_SECTOR ? left : ISO_SECTOR, off = 0;
    while (off + 33 < chunk) {
      uint8_t rec_len = sector[off];
      if (rec_len == 0)
        break; /* padding to end of sector */
      if (rec_len < 34 || off + rec_len > ISO_SECTOR)
        break;
      uint8_t nlen = sector[off + 32];
      const char *name = (const char *)&sector[off + 33];
      if (33u + nlen <= rec_len && nlen >= tlen &&
          memcmp(name, target, tlen) == 0 &&
          (nlen == tlen || name[tlen] == ';')) {
        out->lba = get_u32le(&sector[off + 2]);
        out->size = get_u32le(&sector[off + 10]);
        return 1;
      }
      off += rec_len;
    }
    if (left <= ISO_SECTOR)
      break;
    left -= ISO_SECTOR;
    lba++;
  }
  return 0;
}

/* Parse BOOT2 out of SYSTEM.CNF text. Returns 0 and fills value. */
static int parse_boot2(const char *text, char *value, size_t vsz) {
  const char *p = text;
  while ((p = strstr(p, "BOOT2")) != NULL) {
    /* Must be at line start. */
    if (p != text && p[-1] != '\n' && p[-1] != '\r') {
      p += 5;
      continue;
    }
    p += 5;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p != '=')
      continue;
    p++;
    while (*p == ' ' || *p == '\t')
      p++;
    size_t n = 0;
    while (*p && *p != '\r' && *p != '\n' && n < vsz - 1)
      value[n++] = *p++;
    value[n] = 0;
    str_rtrim(value);
    return value[0] ? 0 : -1;
  }
  return -1;
}

static int has_udf_vrs(GameSource *src) {
  /* The volume recognition sequence follows the ISO descriptors. */
  for (uint32_t lba = PVD_LBA + 1; lba < PVD_LBA + 16; lba++) {
    if (read_sector(src, lba, sector) != ERR_OK)
      return 0;
    if (memcmp(&sector[1], "NSR02", 5) == 0 ||
        memcmp(&sector[1], "NSR03", 5) == 0 ||
        memcmp(&sector[1], "BEA01", 5) == 0)
      return 1;
    if (sector[0] == 255 && memcmp(&sector[1], "CD001", 5) == 0)
      continue; /* terminator; VRS may follow */
  }
  return 0;
}

disc_hint_t iso_hint_from_path(const char *path) {
  disc_hint_t hint = DISC_HINT_NONE;
  const char *p = path;
  while (p && *p) {
    const char *slash = strchr(p, '/');
    const char *end = slash ? slash : p + strlen(p);
    size_t n = (size_t)(end - p);
    if (slash) { /* only directory components count */
      char comp[4] = {0};
      if (n == 2 || n == 3) {
        for (size_t i = 0; i < n; i++)
          comp[i] = (p[i] >= 'a' && p[i] <= 'z') ? p[i] - 32 : p[i];
        if (strcmp(comp, "CD") == 0)
          hint = DISC_HINT_CD;
        else if (strcmp(comp, "DVD") == 0)
          hint = DISC_HINT_DVD;
      }
    }
    p = slash ? slash + 1 : NULL;
  }
  return hint;
}

inst_err_t iso_probe(GameSource *src, disc_hint_t hint, iso_info_t *out) {
  static uint8_t pvd[ISO_SECTOR];
  static char cnf[ISO_SECTOR + 1];
  memset(out, 0, sizeof(*out));

  int64_t size = source_size(src);
  if (size < 0)
    return ERR_SOURCE_READ;
  out->source_size = (uint64_t)size;

  inst_err_t e = read_sector(src, PVD_LBA, pvd);
  if (e)
    return e;
  if (pvd[0] != 0x01 || memcmp(&pvd[1], "CD001", 5) != 0)
    return ERR_SOURCE_INVALID_ISO;

  memcpy(out->volume_id, &pvd[40], 32);
  out->volume_id[32] = 0;
  str_rtrim(out->volume_id);

  uint32_t blocks = get_u32le(&pvd[80]);
  uint32_t bsz = get_u16le(&pvd[128]);
  if (bsz != ISO_SECTOR || blocks <= PVD_LBA)
    return ERR_SOURCE_INVALID_ISO;
  out->pvd_blocks = blocks;

  /* Install the whole logical file: DVD9 images carry layer 1 beyond
   * the layer-0 PVD size. It must be sector aligned and not truncated. */
  if (out->source_size % ISO_SECTOR != 0 ||
      out->source_size < (uint64_t)blocks * ISO_SECTOR ||
      out->source_size / ISO_SECTOR > 0xFFFFFFFFull)
    return ERR_SOURCE_INVALID_ISO;
  out->sectors = (uint32_t)(out->source_size / ISO_SECTOR);

  extent_t root = {get_u32le(&pvd[156 + 2]), get_u32le(&pvd[156 + 10])};
  extent_t cnf_ext;
  int f = find_in_dir(src, root, "SYSTEM.CNF", &cnf_ext);
  if (f < 0)
    return (inst_err_t)(-f);
  if (f == 0)
    return ERR_SOURCE_SYSTEM_CNF;

  uint32_t clen = cnf_ext.size > ISO_SECTOR ? ISO_SECTOR : cnf_ext.size;
  e = read_sector(src, cnf_ext.lba, cnf);
  if (e)
    return e;
  cnf[clen] = 0;
  if (parse_boot2(cnf, out->boot2, sizeof(out->boot2)) < 0)
    return ERR_SOURCE_SYSTEM_CNF;
  if (boot_id_from_boot2(out->boot2, out->boot_id) < 0)
    return ERR_SOURCE_SYSTEM_CNF;
  boot_id_to_part_id(out->boot_id, out->part_id);

  out->has_udf = has_udf_vrs(src);
  if (hint == DISC_HINT_CD)
    out->disc_type = DISC_TYPE_CD;
  else if (hint == DISC_HINT_DVD)
    out->disc_type = DISC_TYPE_DVD;
  else if (out->has_udf || out->sectors > CD_MAX_SECTORS)
    out->disc_type = DISC_TYPE_DVD;
  else
    out->disc_type = DISC_TYPE_CD;

  /* DVD9: OPL treats the PVD volume size as layer 0's maxLBA; a second
   * PVD at that sector means layer 1 starts at maxLBA - 16. */
  out->layer1_start = 0;
  if (out->disc_type == DISC_TYPE_DVD && out->sectors > blocks) {
    if (read_sector(src, blocks, sector) == ERR_OK && sector[0] == 1 &&
        memcmp(&sector[1], "CD001", 5) == 0)
      out->layer1_start = blocks - 16;
  }
  return ERR_OK;
}
