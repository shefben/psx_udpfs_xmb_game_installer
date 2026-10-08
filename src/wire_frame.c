#include <string.h>

#include "lz4_block.h"
#include "wire_frame.h"

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

uint32_t wire_frame_raw_for(uint32_t req) {
  if (req <= WIRE_HDR)
    return 0;
  return (req - WIRE_HDR) / 2048u * 2048u;
}

int wire_frame_parse(const uint8_t *buf, uint32_t avail, uint32_t raw_max, wire_frame_t *f) {
  if (avail < WIRE_HDR || rd32(buf) != WIRE_MAGIC)
    return -1;
  f->raw_len = rd32(buf + 4);
  f->data_len = rd32(buf + 8);
  f->flags = rd32(buf + 12);
  if (f->flags & ~WIRE_F_LZ4)
    return -1;
  if (f->raw_len == 0 || f->raw_len > raw_max)
    return -1;
  if (f->data_len > avail - WIRE_HDR)
    return -1;
  if (!(f->flags & WIRE_F_LZ4) && f->data_len != f->raw_len)
    return -1;
  uint32_t padded = (f->data_len + 15u) & ~15u;
  /* The last frame of a read may end without its padding only if the
   * read ended there; anything else is a framing error. */
  if (padded > avail - WIRE_HDR)
    padded = f->data_len;
  f->total = WIRE_HDR + padded;
  f->data = buf + WIRE_HDR;
  return (int)f->total;
}

int wire_frame_decode(const wire_frame_t *f, uint8_t *out) {
  if (f->flags & WIRE_F_LZ4)
    return lz4_block_decompress(f->data, f->data_len, out, f->raw_len) == (int)f->raw_len ? 0
                                                                                           : -1;
  memcpy(out, f->data, f->raw_len);
  return 0;
}

int wire_path(const char *path, char *out, size_t outsz) {
  static const char dev[] = "udpfs:";
  size_t dl = sizeof(dev) - 1;
  if (!path || strncmp(path, dev, dl) != 0)
    return -1;
  const char *rest = path + dl;
  while (*rest == '/')
    rest++;
  size_t need = dl + strlen(WIRE_PATH_PREFIX) + 1 + strlen(rest) + 1;
  if (!*rest || need > outsz)
    return -1;
  strcpy(out, dev);
  strcat(out, WIRE_PATH_PREFIX);
  strcat(out, "/");
  strcat(out, rest);
  return 0;
}

uint32_t wire_frame_build_stored(const uint8_t *raw, uint32_t raw_len, uint8_t *out) {
  wr32(out, WIRE_MAGIC);
  wr32(out + 4, raw_len);
  wr32(out + 8, raw_len);
  wr32(out + 12, 0);
  memcpy(out + WIRE_HDR, raw, raw_len);
  uint32_t padded = (raw_len + 15u) & ~15u;
  memset(out + WIRE_HDR + raw_len, 0, padded - raw_len);
  return WIRE_HDR + padded;
}
