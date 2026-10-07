#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "source_udpfs.h"

/* Read-ahead buffers, shared by whichever source has async on. A buffer
 * is EMPTY, IN_FLIGHT (fileXio non-blocking read running) or READY
 * (len bytes from logical offset off, or len < 0: that read failed). */
enum { RA_EMPTY = 0, RA_IN_FLIGHT, RA_READY };
static uint8_t ra_buf[2][UDPFS_RA_SIZE] __attribute__((aligned(64)));
static struct {
  udpfs_src_t *owner;
  int64_t off[2];
  int len[2];
  int state[2];
} ra;

int fxio_async_wait(void (*idle)(void *ctx), void *ctx) {
  int r = 0;
  while (fileXioWaitAsync(FXIO_NOWAIT, &r) == FXIO_INCOMPLETE) {
    if (idle)
      idle(ctx);
  }
  fileXioSetBlockMode(FXIO_WAIT);
  return r;
}

static void ra_reset(void) {
  memset(&ra, 0, sizeof(ra));
}

/* Collect the read in flight (if any) into its buffer. */
static void ra_collect(udpfs_src_t *u) {
  for (int b = 0; b < 2; b++) {
    if (ra.state[b] != RA_IN_FLIGHT)
      continue;
    int want = ra.len[b];
    int r = fxio_async_wait(u->idle, u->idle_ctx);
    ra.state[b] = RA_READY;
    ra.len[b] = r;
    /* A short or failed read leaves the device position unknown. */
    u->srv_pos = r == want ? ra.off[b] + r : -1;
  }
}

/* Start reading the block at `off` into buffer b (nothing in flight). */
static void ra_start(udpfs_src_t *u, int b, int64_t off) {
  int64_t left = u->size - off;
  int want = left > UDPFS_RA_SIZE ? UDPFS_RA_SIZE : (int)left;
  ra.off[b] = off;
  ra.state[b] = RA_READY;
  if (u->srv_pos != off) {
    int64_t s = fileXioLseek64(u->fd, off, FIO_SEEK_SET);
    if (s != off) {
      ra.len[b] = s < 0 ? (int)s : -5;
      u->srv_pos = -1;
      return;
    }
    u->srv_pos = off;
  }
  fileXioSetBlockMode(FXIO_NOWAIT);
  int r = fileXioRead(u->fd, ra_buf[b], want);
  if (r < 0) {
    fileXioSetBlockMode(FXIO_WAIT);
    ra.len[b] = r;
    u->srv_pos = -1;
    return;
  }
  ra.len[b] = want; /* expected, until collected */
  ra.state[b] = RA_IN_FLIGHT;
}

static int ra_holds(int b, int64_t pos) {
  return ra.state[b] == RA_READY && ra.len[b] > 0 && pos >= ra.off[b] &&
         pos < ra.off[b] + ra.len[b];
}

static int ra_read_ptr(udpfs_src_t *u, uint32_t max, const uint8_t **out) {
  if (ra.owner != u) {
    ra_reset();
    ra.owner = u;
  }
  if (u->pos >= u->size)
    return 0;
  for (int tries = 0; tries < 4; tries++) {
    int c = ra_holds(0, u->pos) ? 0 : ra_holds(1, u->pos) ? 1 : -1;
    if (c >= 0) {
      int o = 1 - c;
      int64_t end = ra.off[c] + ra.len[c];
      /* The other buffer is free once it is not the next block: start
       * the next read while the caller works on this one. */
      if (ra.state[o] != RA_IN_FLIGHT && !(ra.state[o] == RA_READY && ra.off[o] == end) &&
          end < u->size)
        ra_start(u, o, end);
      int64_t avail = end - u->pos;
      uint32_t n = avail < (int64_t)max ? (uint32_t)avail : max;
      *out = ra_buf[c] + (u->pos - ra.off[c]);
      u->pos += n;
      return (int)n;
    }
    /* A failed read at this position: report it once, then retry. */
    for (int b = 0; b < 2; b++)
      if (ra.state[b] == RA_READY && ra.off[b] == u->pos && ra.len[b] <= 0) {
        int r = ra.len[b];
        ra.state[b] = RA_EMPTY;
        return r;
      }
    if (ra.state[0] == RA_IN_FLIGHT || ra.state[1] == RA_IN_FLIGHT) {
      ra_collect(u);
      continue;
    }
    /* Nothing useful buffered: read here into the emptier buffer. */
    int b = ra.state[0] == RA_EMPTY ? 0 : 1;
    ra_start(u, b, u->pos);
  }
  return -5;
}

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
  u->srv_pos = fileXioLseek64(u->fd, 0, FIO_SEEK_SET) == 0 ? 0 : -1;
  return 0;
}

static void u_set_async(GameSource *src, int mode, void (*idle)(void *ctx), void *ctx) {
  udpfs_src_t *u = src->priv;
  /* Collect without the idle callback: SYNC/OFF callers are about to use
   * the HDD and fileXio themselves. */
  void (*keep)(void *) = u->idle;
  u->idle = NULL;
  if (ra.owner == u)
    ra_collect(u);
  u->idle = keep;
  if (mode == SRC_ASYNC_ON) {
    u->async = 1;
    u->idle = idle;
    u->idle_ctx = ctx;
    if (ra.owner != u) {
      ra_reset();
      ra.owner = u;
    }
  } else if (mode == SRC_ASYNC_OFF) {
    u->async = 0;
    u->idle = NULL;
    if (ra.owner == u)
      ra_reset();
  }
}

static int u_close(GameSource *src) {
  udpfs_src_t *u = src->priv;
  u_set_async(src, SRC_ASYNC_OFF, NULL, NULL);
  int r = 0;
  if (u->fd >= 0)
    r = fileXioClose(u->fd);
  u->fd = -1;
  return r;
}

static int u_read(GameSource *src, void *buf, uint32_t size) {
  udpfs_src_t *u = src->priv;
  if (u->async) {
    const uint8_t *p;
    int n = ra_read_ptr(u, size, &p);
    if (n > 0)
      memcpy(buf, p, (size_t)n);
    return n;
  }
  if (u->pos >= u->size)
    return 0;
  if ((int64_t)size > u->size - u->pos)
    size = (uint32_t)(u->size - u->pos);
  /* udpfs_core_read() reports a mid-transfer failure as a short count
   * and the server position may then be ahead of what we received, so
   * after anything but a complete read the next one seeks first: a retry
   * can never silently skip or duplicate bytes. */
  if (u->srv_pos != u->pos) {
    int64_t s = fileXioLseek64(u->fd, u->pos, FIO_SEEK_SET);
    if (s != u->pos) {
      u->srv_pos = -1;
      return s < 0 ? (int)s : -5;
    }
  }
  int r = fileXioRead(u->fd, buf, (int)size);
  u->srv_pos = r == (int)size ? u->pos + r : -1;
  if (r > 0)
    u->pos += r;
  return r;
}

static int u_read_ptr(GameSource *src, uint32_t max, const uint8_t **out) {
  udpfs_src_t *u = src->priv;
  if (!u->async)
    return -22; /* lending needs the read-ahead buffers */
  return ra_read_ptr(u, max, out);
}

static int64_t u_seek(GameSource *src, int64_t off, int whence) {
  udpfs_src_t *u = src->priv;
  int64_t np = whence == SRC_SEEK_CUR   ? u->pos + off
               : whence == SRC_SEEK_END ? u->size + off
                                        : off;
  if (np < 0)
    return -22;
  u->pos = np; /* buffered blocks stay valid: they are keyed by offset */
  return np;
}

static int64_t u_size(GameSource *src) { return ((udpfs_src_t *)src->priv)->size; }

static int u_read_ptr_any(GameSource *src, uint32_t max, const uint8_t **out);

static const GameSourceOps UDPFS_OPS = {u_open,  u_close,     u_read,         u_seek,
                                        u_size, u_set_async, u_read_ptr_any};

/* read_ptr only lends while async; otherwise the copy loop reads itself. */
static int u_read_ptr_any(GameSource *src, uint32_t max, const uint8_t **out) {
  udpfs_src_t *u = src->priv;
  if (!u->async) {
    if (u->pos >= u->size)
      return 0;
    uint32_t n = max > UDPFS_RA_SIZE ? UDPFS_RA_SIZE : max;
    int r = u_read(src, ra_buf[0], n);
    if (ra.owner)
      ra_reset();
    *out = ra_buf[0];
    return r;
  }
  return u_read_ptr(src, max, out);
}

void source_udpfs_init(GameSource *src, udpfs_src_t *u) {
  memset(src, 0, sizeof(*src));
  memset(u, 0, sizeof(*u));
  u->fd = -1;
  u->srv_pos = -1;
  src->ops = &UDPFS_OPS;
  src->priv = u;
}
