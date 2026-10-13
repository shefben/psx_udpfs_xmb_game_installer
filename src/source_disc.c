#include <limits.h>
#include <string.h>
#include "source_disc.h"
#include "pops.h"

int source_is_disc(const char *path) { return path && !strncmp(path, "disc:/", 6); }
static int bcd(uint8_t v) { return (v & 15) > 9 || (v >> 4) > 9 ? -1 : (v >> 4) * 10 + (v & 15); }
static int msf(const uint8_t *p) {
  int m = bcd(p[0]), s = bcd(p[1]), f = bcd(p[2]);
  return m < 0 || s < 0 || s >= 60 || f < 0 || f >= 75 ? -1 : (m * 60 + s) * 75 + f - 150;
}
int disc_toc_sectors(const uint8_t toc[1024], uint32_t *sectors) {
  int first = bcd(toc[7]), last = bcd(toc[17]), end = msf(toc + 27);
  if (toc[2] != 0xa0 || toc[12] != 0xa1 || toc[22] != 0xa2 ||
      first != 1 || last < 1 || last > 99 || end <= 0) return -1;
  int prev = -1;
  for (int i = 0; i < last; i++) {
    const uint8_t *t = toc + 30 + i * 10;
    int start = msf(t + 7);
    if (bcd(t[2]) != i + 1 || start < 0 || start <= prev || start >= end) return -1;
    prev = start;
  }
  if (msf(toc + 37) != 0 || !(toc[30] & 0x40)) return -1;
  *sectors = (uint32_t)end;
  return 0;
}
static int disc_open(GameSource *s, const char *path) {
  disc_src_t *d = s->priv;
  if (!source_is_disc(path) || !d->prepare || !d->read) return -22;
  memset(&d->info, 0, sizeof(d->info));
  int r = d->prepare(d->ctx, &d->info);
  if (r < 0) return r;
  int vcd = strstr(path, "/PS1/") != NULL;
  if (!d->info.sectors || vcd != d->info.ps1 ||
      (!vcd && ((strstr(path, "/DVD/") != NULL) != d->info.dvd))) return -22;
  memset(d->header, 0, sizeof(d->header));
  if (vcd) {
    uint32_t sectors;
    if (disc_toc_sectors(d->info.toc, &sectors) < 0 || sectors != d->info.sectors) return -22;
    /* POPS uses the same ten-byte track descriptors as the CD TOC. */
    memcpy(d->header, d->info.toc, 1024);
    d->header[8] = 0x20; /* CD-XA */
    int tracks = bcd(d->header[17]);
    for (int i = 0; i < tracks; i++) {
      uint8_t *t = d->header + 30 + i * 10;
      t[0] = (t[0] & 0x40) ? 0x41 : 0x01;
      t[1] = t[6] = 0;
      /* TOC has INDEX 01 only. Preserve its start for INDEX 00 rather
       * than inventing a pregap or moving any audio samples. */
      if (i) memcpy(t + 3, t + 7, 3);
      else memset(t + 3, 0, 3);
    }
    d->header[10] = d->header[20] = d->header[30 + (tracks - 1) * 10];
    for (int i = 0; i < 4; i++) {
      d->header[1032 + i] = (uint8_t)(sectors >> (8 * i));
      d->header[1036 + i] = (uint8_t)(sectors >> (8 * i));
    }
  }
  d->pos = 0;
  d->cache_n = 0;
  d->bytes = (vcd ? VCD_HEADER_SIZE : 0) + (uint64_t)d->info.sectors * (vcd ? 2352 : 2048);
  return 0;
}
static int disc_close(GameSource *s) { ((disc_src_t *)s->priv)->cache_n = 0; return 0; }
static int64_t disc_size(GameSource *s) { return (int64_t)((disc_src_t *)s->priv)->bytes; }
static int64_t disc_seek(GameSource *s, int64_t offset, int whence) {
  disc_src_t *d = s->priv;
  int64_t base = whence == SRC_SEEK_SET ? 0 : whence == SRC_SEEK_CUR ? (int64_t)d->pos :
                 whence == SRC_SEEK_END ? (int64_t)d->bytes : -1;
  if (base < 0 || offset < -base || offset > (int64_t)d->bytes - base) return -22;
  d->pos = (uint64_t)(base + offset);
  return (int64_t)d->pos;
}
static int disc_read(GameSource *s, void *buf, uint32_t size) {
  disc_src_t *d = s->priv;
  uint8_t *out = buf;
  uint32_t done = 0;
  if (size > INT_MAX) size = INT_MAX;
  if (size > d->bytes - d->pos) size = (uint32_t)(d->bytes - d->pos);
  while (done < size) {
    uint32_t take = size - done;
    if (d->info.ps1 && d->pos < VCD_HEADER_SIZE) {
      if (take > VCD_HEADER_SIZE - d->pos) take = (uint32_t)(VCD_HEADER_SIZE - d->pos);
      memset(out + done, 0, take);
      if (d->pos < sizeof(d->header)) {
        uint32_t h = sizeof(d->header) - (uint32_t)d->pos;
        if (h > take) h = take;
        memcpy(out + done, d->header + d->pos, h);
      }
    } else {
      uint32_t unit = d->info.ps1 ? 2352 : 2048;
      uint64_t pos = d->pos - (d->info.ps1 ? VCD_HEADER_SIZE : 0);
      uint32_t lba = (uint32_t)(pos / unit), offset = (uint32_t)(pos % unit);
      if (!d->cache_n || lba < d->cache_lba || lba >= d->cache_lba + d->cache_n) {
        d->cache_lba = lba;
        d->cache_n = d->info.sectors - lba;
        if (d->cache_n > DISC_CACHE_SECTORS) d->cache_n = DISC_CACHE_SECTORS;
        int r = d->read(d->ctx, lba, d->cache_n, d->info.ps1, d->cache);
        if (r < 0) { d->cache_n = 0; return r; }
      }
      uint32_t start = (lba - d->cache_lba) * unit + offset;
      if (take > d->cache_n * unit - start) take = d->cache_n * unit - start;
      memcpy(out + done, d->cache + start, take);
    }
    d->pos += take;
    done += take;
  }
  return (int)done;
}
static const GameSourceOps ops = {disc_open, disc_close, disc_read, disc_seek, disc_size, NULL, NULL};
void source_disc_init(GameSource *s, disc_src_t *d, disc_prepare_fn prepare,
                      disc_read_fn read, void *ctx) {
  memset(s, 0, sizeof(*s));
  memset(d, 0, sizeof(*d));
  d->prepare = prepare; d->read = read; d->ctx = ctx;
  s->ops = &ops; s->priv = d;
}
