#include <string.h>

#include "source.h"
#include "util.h"

source_type_t source_classify(const char *name) {
  if (name && strlen(name) > 4 && str_ends_with_ci(name, ".zso"))
    return SRC_TYPE_ZSO_FILE;
  if (!name || strlen(name) <= 4 || !str_ends_with_ci(name, ".iso"))
    return SRC_TYPE_NONE;
  if (str_ends_with_ci(name, ".zso.iso"))
    return strlen(name) > 8 ? SRC_TYPE_ZSO : SRC_TYPE_NONE;
  /* Other udpfsd virtual images are deliberately not offered in v1. */
  if (str_ends_with_ci(name, ".cso.iso") || str_ends_with_ci(name, ".chd.iso"))
    return SRC_TYPE_NONE;
  return SRC_TYPE_ISO;
}

const char *source_type_label(source_type_t t) {
  switch (t) {
  case SRC_TYPE_ISO:
    return "ISO";
  case SRC_TYPE_ZSO:
  case SRC_TYPE_ZSO_FILE:
    return "ZSO";
  default:
    return "";
  }
}

int source_is_raw_zso(const char *path) {
  return path && str_ends_with_ci(path, ".zso");
}

inst_err_t source_open(GameSource *src, const char *path) {
  src->last_rc = 0;
  src->is_open = 0;
  str_copy(src->path, path, sizeof(src->path));
  int rc = src->ops->open(src, path);
  if (rc < 0) {
    src->last_rc = rc;
    return ERR_SOURCE_OPEN;
  }
  src->is_open = 1;
  return ERR_OK;
}

void source_close(GameSource *src) {
  if (src->is_open)
    src->ops->close(src);
  src->is_open = 0;
}

int64_t source_size(GameSource *src) {
  int64_t s = src->ops->size(src);
  if (s < 0)
    src->last_rc = (int)s;
  return s;
}

inst_err_t source_read_exact(GameSource *src, void *buf, uint32_t size) {
  uint8_t *p = buf;
  while (size > 0) {
    int got = src->ops->read(src, p, size);
    if (got < 0) {
      src->last_rc = got;
      return ERR_SOURCE_READ;
    }
    if (got == 0) {
      /* Premature EOF: still a read error, but record no driver code. */
      src->last_rc = 0;
      return ERR_SOURCE_READ;
    }
    p += got;
    size -= (uint32_t)got;
  }
  return ERR_OK;
}

inst_err_t source_read_at(GameSource *src, uint64_t offset, void *buf,
                          uint32_t size) {
  int64_t r = src->ops->seek(src, (int64_t)offset, SRC_SEEK_SET);
  if (r < 0 || (uint64_t)r != offset) {
    src->last_rc = r < 0 ? (int)r : 0;
    return ERR_SOURCE_READ;
  }
  return source_read_exact(src, buf, size);
}

/* ---- memsrc ------------------------------------------------------ */

static int mem_open(GameSource *src, const char *path) {
  (void)path;
  ((memsrc_t *)src->priv)->pos = 0;
  return 0;
}

static int mem_close(GameSource *src) {
  (void)src;
  return 0;
}

static int mem_read(GameSource *src, void *buf, uint32_t size) {
  memsrc_t *m = src->priv;
  m->reads++;
  if (m->fail_after_reads > 0 && m->reads >= m->fail_after_reads)
    return -5; /* -EIO */
  if (m->pos >= m->total_size)
    return 0;
  if (m->max_chunk && size > m->max_chunk)
    size = m->max_chunk;
  if (size > m->total_size - m->pos)
    size = (uint32_t)(m->total_size - m->pos);
  memset(buf, 0, size);
  for (int i = 0; i < m->nsegs; i++) {
    const memsrc_seg_t *s = &m->segs[i];
    uint64_t lo = s->offset > m->pos ? s->offset : m->pos;
    uint64_t hi_s = s->offset + s->len, hi_r = m->pos + size;
    uint64_t hi = hi_s < hi_r ? hi_s : hi_r;
    if (lo < hi)
      memcpy((uint8_t *)buf + (lo - m->pos), s->data + (lo - s->offset),
             (size_t)(hi - lo));
  }
  m->pos += size;
  return (int)size;
}

static int64_t mem_seek(GameSource *src, int64_t off, int whence) {
  memsrc_t *m = src->priv;
  int64_t base = whence == SRC_SEEK_CUR   ? (int64_t)m->pos
                 : whence == SRC_SEEK_END ? (int64_t)m->total_size
                                          : 0;
  int64_t np = base + off;
  if (np < 0)
    return -22; /* -EINVAL */
  m->pos = (uint64_t)np;
  return np;
}

static int64_t mem_size(GameSource *src) {
  return (int64_t)((memsrc_t *)src->priv)->total_size;
}

static const GameSourceOps MEM_OPS = {mem_open, mem_close, mem_read, mem_seek,
                                      mem_size};

void memsrc_init(GameSource *src, memsrc_t *m, uint64_t total_size) {
  memset(m, 0, sizeof(*m));
  memset(src, 0, sizeof(*src));
  m->total_size = total_size;
  src->ops = &MEM_OPS;
  src->priv = m;
  src->is_open = 1;
}

void memsrc_add(memsrc_t *m, uint64_t offset, const void *data, uint32_t len) {
  if (m->nsegs < MEMSRC_MAX_SEGS) {
    m->segs[m->nsegs].offset = offset;
    m->segs[m->nsegs].data = data;
    m->segs[m->nsegs].len = len;
    m->nsegs++;
  }
}
