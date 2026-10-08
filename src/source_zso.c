#include <string.h>
#include <strings.h>

#include "lz4_block.h"
#include "source_zso.h"

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t index_entries(const zso_src_t *z) {
  return z->ziso ? z->num_blocks + 1 : z->num_blocks;
}

/* Make index entries b (and b + 1 where it exists) available. */
static int load_index(zso_src_t *z, uint32_t b) {
  uint32_t need_last = b + 1 < index_entries(z) ? b + 1 : b;
  if (z->idx_count && b >= z->idx_first && need_last < z->idx_first + z->idx_count)
    return 0;
  uint32_t count = index_entries(z) - b;
  if (count > ZSO_INDEX_WINDOW + 1)
    count = ZSO_INDEX_WINDOW + 1;
  static uint8_t raw[(ZSO_INDEX_WINDOW + 1) * 4];
  if (source_read_at(z->inner, (uint64_t)z->header_size + 4ull * b, raw, count * 4) != ERR_OK)
    return -5;
  for (uint32_t i = 0; i < count; i++)
    z->idx[i] = le32(raw + 4 * i);
  z->idx_first = b;
  z->idx_count = count;
  return 0;
}

static uint64_t entry_offset(const zso_src_t *z, uint32_t raw) {
  return (uint64_t)(raw & 0x7FFFFFFFu) << z->align;
}

/* Decompress block b into z->block. */
static int load_block(zso_src_t *z, uint32_t b) {
  if (z->cur_block == (int64_t)b)
    return 0;
  if (load_index(z, b) < 0)
    return -5;
  uint32_t raw = z->idx[b - z->idx_first];
  uint64_t off = entry_offset(z, raw);
  uint64_t end = b + 1 < index_entries(z) ? entry_offset(z, z->idx[b + 1 - z->idx_first])
                                          : z->inner_size;
  if (end < off || end - off > ZSO_CHUNK || end > z->inner_size)
    return -5;
  uint32_t clen = (uint32_t)(end - off);
  /* Fetch a run of compressed blocks with one inner read. */
  if (off < z->chunk_off || end > z->chunk_off + z->chunk_len) {
    uint64_t len = z->inner_size - off;
    if (len > ZSO_CHUNK)
      len = ZSO_CHUNK;
    if (source_read_at(z->inner, off, z->chunk, (uint32_t)len) != ERR_OK)
      return -5;
    z->chunk_off = off;
    z->chunk_len = (uint32_t)len;
  }
  const uint8_t *c = z->chunk + (off - z->chunk_off);
  uint64_t left = z->size - (uint64_t)b * z->block_size;
  uint32_t want = left < z->block_size ? (uint32_t)left : z->block_size;
  if (raw & 0x80000000u) {
    if (clen < want)
      return -5;
    memcpy(z->block, c, want);
  } else if (lz4_block_decompress(c, clen, z->block, want) != (int)want) {
    return -5;
  }
  z->cur_block = b;
  return 0;
}

static int z_open(GameSource *src, const char *path) {
  zso_src_t *z = src->priv;
  char raw[SOURCE_PATH_MAX];
  size_t n = strlen(path);
  if (z->strip_iso && n > 8 && !strcasecmp(path + n - 8, ".zso.iso")) {
    memcpy(raw, path, n - 4);
    raw[n - 4] = 0;
    path = raw;
  }
  if (source_open(z->inner, path) != ERR_OK)
    return z->inner->last_rc ? z->inner->last_rc : -5;
  uint8_t h[24];
  int64_t isz = source_size(z->inner);
  if (isz < 24 || source_read_at(z->inner, 0, h, sizeof(h)) != ERR_OK)
    goto bad;
  z->ziso = !memcmp(h, "ZISO", 4);
  if (!z->ziso && memcmp(h, "ZSO\0", 4))
    goto bad;
  z->header_size = le32(h + 4);
  z->size = (uint64_t)le32(h + 8) | ((uint64_t)le32(h + 12) << 32);
  z->block_size = le32(h + 16);
  z->align = z->ziso ? h[21] : 0;
  z->inner_size = (uint64_t)isz;
  if (z->header_size < 24 || z->block_size < 512 || z->block_size > ZSO_MAX_BLOCK ||
      (z->block_size & (z->block_size - 1)) || z->size == 0 || z->align > 16)
    goto bad;
  uint64_t nb = (z->size + z->block_size - 1) / z->block_size;
  if (nb > 0x7FFFFFFFu)
    goto bad;
  z->num_blocks = (uint32_t)nb;
  z->pos = 0;
  z->idx_count = 0;
  z->chunk_len = 0;
  z->cur_block = -1;
  z->using_fallback = 0;
  return 0;
bad:
  source_close(z->inner);
  return -22; /* not a ZSO this reader supports */
}

static int z_close(GameSource *src) {
  zso_src_t *z = src->priv;
  source_close(z->inner);
  if (z->fallback)
    source_close(z->fallback);
  z->using_fallback = 0;
  return 0;
}

/* Switch to the fallback source (same logical bytes) for good. */
static int use_fallback(GameSource *src, zso_src_t *z) {
  if (!z->fallback || z->using_fallback)
    return -1;
  if (source_open(z->fallback, src->path) != ERR_OK)
    return -1;
  if (source_size(z->fallback) != (int64_t)z->size) {
    source_close(z->fallback);
    return -1;
  }
  z->using_fallback = 1;
  return 0;
}

static int z_read(GameSource *src, void *buf, uint32_t size) {
  zso_src_t *z = src->priv;
  uint8_t *out = buf;
  uint32_t done = 0;
  while (done < size && z->pos < z->size) {
    if (z->using_fallback) {
      uint64_t left = z->size - z->pos;
      uint32_t n = size - done < left ? size - done : (uint32_t)left;
      if (source_read_at(z->fallback, z->pos, out + done, n) != ERR_OK)
        return done ? (int)done : (z->fallback->last_rc ? z->fallback->last_rc : -5);
      done += n;
      z->pos += n;
      continue;
    }
    uint32_t b = (uint32_t)(z->pos / z->block_size);
    uint32_t in_blk = (uint32_t)(z->pos % z->block_size);
    if (load_block(z, b) < 0) {
      if (use_fallback(src, z) == 0)
        continue;
      return done ? (int)done : -5;
    }
    uint64_t blk_end = (uint64_t)b * z->block_size + z->block_size;
    if (blk_end > z->size)
      blk_end = z->size;
    uint32_t n = (uint32_t)(blk_end - z->pos);
    if (n > size - done)
      n = size - done;
    memcpy(out + done, z->block + in_blk, n);
    done += n;
    z->pos += n;
  }
  return (int)done;
}

static int64_t z_seek(GameSource *src, int64_t off, int whence) {
  zso_src_t *z = src->priv;
  int64_t np = whence == SRC_SEEK_CUR   ? (int64_t)z->pos + off
               : whence == SRC_SEEK_END ? (int64_t)z->size + off
                                        : off;
  if (np < 0)
    return -22;
  z->pos = (uint64_t)np;
  return np;
}

static int64_t z_size(GameSource *src) { return (int64_t)((zso_src_t *)src->priv)->size; }

/* No read-ahead: index and data reads alternate (source_udpfs.h). */
static const GameSourceOps ZSO_OPS = {z_open, z_close, z_read, z_seek, z_size, NULL, NULL};

void source_zso_init(GameSource *src, zso_src_t *z, GameSource *inner) {
  memset(src, 0, sizeof(*src));
  z->inner = inner;
  z->strip_iso = 0;
  z->fallback = NULL;
  z->using_fallback = 0;
  z->idx_count = 0;
  z->chunk_len = 0;
  z->cur_block = -1;
  src->ops = &ZSO_OPS;
  src->priv = z;
}
