#ifndef PSXI_SOURCE_DISC_H
#define PSXI_SOURCE_DISC_H
#include "source.h"

/* A physical disc exposed as an ISO (PS2) or a generated POPStarter VCD
 * (PS1). The hardware backend returns complete 2352-byte sectors for PS1,
 * including audio tracks, and 2048-byte sectors for PS2. */
typedef struct {
  uint32_t sectors, layer1_start;
  int ps1, dvd;
  uint8_t toc[1024];
} disc_info_t;
typedef int (*disc_prepare_fn)(void *ctx, disc_info_t *info);
typedef int (*disc_read_fn)(void *ctx, uint32_t lba, uint32_t sectors, int raw, void *out);
#define DISC_CACHE_SECTORS 16
typedef struct {
  disc_prepare_fn prepare;
  disc_read_fn read;
  void *ctx;
  disc_info_t info;
  uint64_t pos, bytes;
  uint32_t cache_lba, cache_n;
  uint8_t header[1040];
  uint8_t cache[DISC_CACHE_SECTORS * 2352] __attribute__((aligned(64)));
} disc_src_t;
void source_disc_init(GameSource *src, disc_src_t *d, disc_prepare_fn prepare,
                      disc_read_fn read, void *ctx);
int source_is_disc(const char *path);
/* Validates the CD TOC and converts its lead-out to a sector count. */
int disc_toc_sectors(const uint8_t toc[1024], uint32_t *sectors);
#endif
