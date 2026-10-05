#ifndef PSXI_SOURCE_ZSO_H
#define PSXI_SOURCE_ZSO_H

#include "source.h"

/* ZSO / ZISO (LZ4, maxcso) image read through an inner GameSource as the
 * plain ISO it contains: what udpfsd does for "<name>.zso.iso", done on
 * the PS2 for USB. Index entries are cached in windows; consecutive
 * compressed blocks are fetched with one inner read. */

#define ZSO_INDEX_WINDOW 4096           /* index entries cached */
#define ZSO_CHUNK (512 * 1024)          /* compressed bytes per inner read */
#define ZSO_MAX_BLOCK 8192              /* largest supported block size */

typedef struct {
  GameSource *inner;
  /* Open "<x>.zso" for the path "<x>.zso.iso" (udpfsd's virtual name):
   * the raw, still compressed file goes over the network. */
  int strip_iso;
  uint64_t size;      /* uncompressed */
  uint32_t block_size;
  uint32_t num_blocks;
  uint32_t header_size;
  uint8_t align;
  int ziso; /* "ZISO" (index has num_blocks + 1 entries) vs "ZSO\0" */
  uint64_t inner_size;
  uint64_t pos;
  /* index window: entries [idx_first, idx_first + idx_count) */
  uint32_t idx_first, idx_count;
  uint32_t idx[ZSO_INDEX_WINDOW + 1];
  /* compressed bytes [chunk_off, chunk_off + chunk_len) of the inner file */
  uint64_t chunk_off;
  uint32_t chunk_len;
  uint8_t chunk[ZSO_CHUNK];
  /* last decompressed block */
  int64_t cur_block;
  uint8_t block[ZSO_MAX_BLOCK];
} zso_src_t;

/* The inner source must not be open yet: source_open() on `src` opens
 * it with the same path. */
void source_zso_init(GameSource *src, zso_src_t *z, GameSource *inner);

#endif
