#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "source_udpfs.h"

static int u_open(GameSource *src, const char *path) {
  udpfs_src_t *u = src->priv;
  u->fd = fileXioOpen(path, FIO_O_RDONLY);
  if (u->fd < 0)
    return u->fd;
  u->pos = 0;
  /* lseek64 keeps sizes above 2/4 GiB intact (PS2SDK off_t is 32-bit). */
  u->size = fileXioLseek64(u->fd, 0, FIO_SEEK_END);
  if (u->size < 0) {
    int rc = (int)u->size;
    fileXioClose(u->fd);
    u->fd = -1;
    return rc;
  }
  fileXioLseek64(u->fd, 0, FIO_SEEK_SET);
  return 0;
}

static int u_close(GameSource *src) {
  udpfs_src_t *u = src->priv;
  int r = 0;
  if (u->fd >= 0)
    r = fileXioClose(u->fd);
  u->fd = -1;
  return r;
}

static int u_read(GameSource *src, void *buf, uint32_t size) {
  udpfs_src_t *u = src->priv;
  if (u->pos >= u->size)
    return 0;
  if ((int64_t)size > u->size - u->pos)
    size = (uint32_t)(u->size - u->pos);
  /* udpfs_core_read() reports a mid-transfer failure as a short count
   * and the server position may then be ahead of what we received.
   * Re-seek to our own position before every read so a retry can never
   * silently skip or duplicate bytes. */
  int64_t s = fileXioLseek64(u->fd, u->pos, FIO_SEEK_SET);
  if (s != u->pos)
    return s < 0 ? (int)s : -5;
  int r = fileXioRead(u->fd, buf, (int)size);
  if (r > 0)
    u->pos += r;
  return r;
}

static int64_t u_seek(GameSource *src, int64_t off, int whence) {
  udpfs_src_t *u = src->priv;
  int64_t np = whence == SRC_SEEK_CUR   ? u->pos + off
               : whence == SRC_SEEK_END ? u->size + off
                                        : off;
  if (np < 0)
    return -22;
  u->pos = np;
  return np;
}

static int64_t u_size(GameSource *src) { return ((udpfs_src_t *)src->priv)->size; }

static const GameSourceOps UDPFS_OPS = {u_open, u_close, u_read, u_seek, u_size};

void source_udpfs_init(GameSource *src, udpfs_src_t *u) {
  memset(src, 0, sizeof(*src));
  memset(u, 0, sizeof(*u));
  u->fd = -1;
  src->ops = &UDPFS_OPS;
  src->priv = u;
}
