#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "pops.h"
#include "util.h"

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* User data of raw sector `lba` (Mode 2 Form 1: 24 bytes of sync,
 * header and subheader precede the 2048 data bytes). */
static inst_err_t read_sector(GameSource *src, uint32_t lba, uint8_t buf[2048]) {
  return source_read_at(src, VCD_HEADER_SIZE + (uint64_t)lba * VCD_RAW_SECTOR + 24, buf, 2048);
}

/* Game ID from a SYSTEM.CNF text: "BOOT = cdrom:\SLUS_005.94;1". */
static int boot_id_from_cnf(const char *text, char out[16]) {
  for (const char *p = text; *p;) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!strncasecmp(p, "BOOT", 4) && (p[4] == ' ' || p[4] == '=' || p[4] == '\t')) {
      const char *v = strchr(p, '=');
      if (!v)
        return -1;
      char line[96];
      size_t n = strcspn(v + 1, "\r\n");
      if (n >= sizeof(line))
        n = sizeof(line) - 1;
      memcpy(line, v + 1, n);
      line[n] = 0;
      return boot_id_from_boot2(line, out);
    }
    const char *nl = strchr(p, '\n');
    if (!nl)
      break;
    p = nl + 1;
  }
  return -1;
}

inst_err_t vcd_probe(GameSource *src, vcd_info_t *out) {
  memset(out, 0, sizeof(*out));
  int64_t size = source_size(src);
  if (size < (int64_t)(VCD_HEADER_SIZE + 24ull * VCD_RAW_SECTOR) ||
      (size - VCD_HEADER_SIZE) % VCD_RAW_SECTOR)
    return ERR_SOURCE_INVALID_ISO;
  out->bytes = (uint64_t)size;
  out->sectors = (uint32_t)((size - VCD_HEADER_SIZE) / VCD_RAW_SECTOR);

  static uint8_t s[2048];
  inst_err_t e = read_sector(src, 16, s);
  if (e)
    return e;
  if (memcmp(s, "\x01" "CD001", 6))
    return ERR_SOURCE_INVALID_ISO;
  memcpy(out->volume_id, s + 40, 32);
  out->volume_id[32] = 0;
  for (int i = 31; i >= 0 && out->volume_id[i] == ' '; i--)
    out->volume_id[i] = 0;
  uint32_t root_lba = le32(s + 156 + 2), root_len = le32(s + 156 + 10);
  if (root_lba >= out->sectors || root_len == 0)
    return ERR_SOURCE_INVALID_ISO;

  /* Root directory: find SYSTEM.CNF. */
  uint32_t cnf_lba = 0, cnf_len = 0;
  for (uint32_t done = 0; done < root_len && !cnf_lba; done += 2048) {
    if ((e = read_sector(src, root_lba + done / 2048, s)))
      return e;
    for (uint32_t off = 0; off < 2048 && s[off];) {
      const uint8_t *r = s + off;
      uint8_t nl = r[32];
      if (r[0] < 33 || off + r[0] > 2048 || 33u + nl > r[0])
        break;
      if (nl >= 10 && !strncasecmp((const char *)r + 33, "SYSTEM.CNF", 10) &&
          (nl == 10 || r[33 + 10] == ';')) {
        cnf_lba = le32(r + 2);
        cnf_len = le32(r + 10);
        break;
      }
      off += r[0];
    }
  }
  if (!cnf_lba || cnf_lba >= out->sectors)
    return ERR_SOURCE_SYSTEM_CNF;
  if ((e = read_sector(src, cnf_lba, s)))
    return e;
  char text[2049];
  uint32_t n = cnf_len < 2048 ? cnf_len : 2048;
  memcpy(text, s, n);
  text[n] = 0;
  if (boot_id_from_cnf(text, out->boot_id) < 0 ||
      boot_id_to_part_id(out->boot_id, out->part_id) < 0)
    return ERR_SOURCE_SYSTEM_CNF;
  return ERR_OK;
}

int pops_partition_mb(uint64_t vcd_bytes, char *size_str, size_t sz) {
  uint64_t need = (vcd_bytes >> 20) + 1 + 8;
  for (int mb = 128; mb <= 2048; mb *= 2)
    if (need <= (uint64_t)mb) {
      if (mb >= 1024)
        snprintf(size_str, sz, "%dG", mb / 1024);
      else
        snprintf(size_str, sz, "%dM", mb);
      return mb;
    }
  return -1;
}

void pops_vmc_dir(const char *partition, char *out, size_t outsz) {
  str_copy(out, strncmp(partition, "PP.", 3) ? partition : partition + 3, outsz);
}
