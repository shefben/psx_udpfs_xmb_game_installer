#include <string.h>

#include "hdl_header.h"
#include "partname.h"
#include "util.h"

void hdl_format_args_build(struct HDLFS_FormatArgs *a, const iso_info_t *iso,
                           const char *title) {
  memset(a, 0, sizeof(*a));
  a->DiscType = iso->disc_type;
  a->NumSectors = iso->sectors;
  a->Layer1Start = iso->layer1_start;
  str_copy(a->GameTitle, title, sizeof(a->GameTitle));
  str_copy(a->StartupPath, iso->boot_id, sizeof(a->StartupPath));
}

/* hdl_game_info offsets (hdlfs.h): magic 0x00, version 0x06,
 * gamename 0x08[160], compat 0xA8..0xAB, startup 0xAC[60],
 * layer1_start 0xE8, discType 0xEC, num_partitions 0xF0,
 * part_specs 0xF4 + 12*i { part_offset, data_start, part_size }. */
int hdl_header_parse(const uint8_t buf[HDL_HEADER_SIZE], hdl_header_info_t *out) {
  memset(out, 0, sizeof(*out));
  out->magic = get_u32le(buf);
  uint16_t mk = get_u16le(buf + HDL_MARK_OFFSET);
  out->marker = (mk == HDL_MARK_INCOMPLETE || mk == HDL_MARK_COMPLETE) ? mk : HDL_MARK_NONE;
  out->version = get_u16le(buf + 6);
  memcpy(out->title, buf + 0x08, HDLFS_GAME_TITLE_LEN);
  out->title[HDLFS_GAME_TITLE_LEN] = 0;
  memcpy(out->startup, buf + 0xAC, HDLFS_STARTUP_PTH_LEN);
  out->startup[HDLFS_STARTUP_PTH_LEN] = 0;
  out->layer1_start = get_u32le(buf + 0xE8);
  out->disc_type = get_u32le(buf + 0xEC);
  out->num_partitions = (int)get_u32le(buf + 0xF0);
  if (out->magic != HDL_INFO_MAGIC || out->num_partitions < 1 ||
      out->num_partitions > HDL_MAX_PART_SPECS)
    return -1;
  for (int i = 0; i < out->num_partitions; i++)
    out->data_bytes += get_u32le(buf + 0xF4 + 12 * i + 8);
  return 0;
}

inst_err_t hdl_header_check(const hdl_header_info_t *h, const char *boot_id,
                            uint8_t disc_type, uint32_t layer1_start,
                            int expected_parts, uint64_t expected_bytes) {
  if (h->magic != HDL_INFO_MAGIC || strcmp(h->startup, boot_id) != 0 ||
      h->title[0] == 0 || h->disc_type != disc_type ||
      h->layer1_start != layer1_start || h->num_partitions != expected_parts ||
      h->data_bytes != expected_bytes)
    return ERR_HDL_VERIFY;
  return ERR_OK;
}

void hdl_header_set_marker(uint8_t *buf, uint16_t marker) {
  buf[HDL_MARK_OFFSET] = (uint8_t)marker;
  buf[HDL_MARK_OFFSET + 1] = (uint8_t)(marker >> 8);
}

int kelf_looks_valid(const void *data, uint32_t size) {
  const uint8_t *b = data;
  /* A KELF header + bit table + signatures is well over 1 KiB; a raw
   * ELF under a .KELF name is the specific mistake to catch. */
  if (!b || size < 1024)
    return 0;
  if (memcmp(b, "\x7f" "ELF", 4) == 0)
    return 0;
  return 1;
}

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int png_basic_valid(const void *data, uint32_t size) {
  const uint8_t *b = data;
  if (!b || size < 33 || size > 4 * 1024 * 1024)
    return 0;
  if (memcmp(b, "\x89PNG\r\n\x1a\n", 8) != 0 || be32(b + 8) != 13 ||
      memcmp(b + 12, "IHDR", 4) != 0)
    return 0;
  uint32_t w = be32(b + 16), h = be32(b + 20);
  return w >= 1 && w <= 1024 && h >= 1 && h <= 1024;
}

void default_display_title(const iso_info_t *iso, const char *source_path,
                           char *out, size_t outsz) {
  int vol_is_id = iso->volume_id[0] == 0 ||
                  strcmp(iso->volume_id, iso->boot_id) == 0 ||
                  strcmp(iso->volume_id, iso->part_id) == 0;
  if (!vol_is_id) {
    str_copy(out, iso->volume_id, outsz);
    return;
  }
  const char *base = strrchr(source_path, '/');
  base = base ? base + 1 : source_path;
  const char *colon = strrchr(base, ':');
  if (colon)
    base = colon + 1;
  str_copy(out, base, outsz);
  size_t n = strlen(out);
  if (str_ends_with_ci(out, ".zso.iso"))
    out[n - 8] = 0;
  else if (str_ends_with_ci(out, ".iso"))
    out[n - 4] = 0;
}
