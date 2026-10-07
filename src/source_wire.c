#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "source_udpfs.h"
#include "source_wire.h"
#include "wire_frame.h"

/* Two network buffers (one being parsed, one being fetched) and one for
 * a decompressed frame. A net buffer holds the frames of one read: they
 * start at raw offset `raw` and `parsed` bytes of them are used up; once
 * all are parsed, `raw` is the raw offset right after them. */
enum { NB_EMPTY = 0, NB_IN_FLIGHT, NB_READY };
static uint8_t nbuf[2][WIRE_REQ_MAX] __attribute__((aligned(64)));
static uint8_t rbuf[WIRE_REQ_MAX] __attribute__((aligned(64)));
static struct {
  wire_src_t *owner;
  int state[2];
  int len[2];      /* bytes received (or <0: that read failed) */
  int parsed[2];
  int64_t raw[2];  /* raw offset of the next unparsed frame */
  /* the raw bytes being handed out: one frame */
  const uint8_t *view;
  int64_t view_off;
  uint32_t view_len;
} nb;
static uint32_t g_req = 64 * 1024;

void source_wire_set_request(uint32_t bytes) {
  if (bytes > WIRE_REQ_MAX)
    bytes = WIRE_REQ_MAX;
  bytes &= ~63u;
  if (bytes >= 4096)
    g_req = bytes;
}

static void nb_reset(void) { memset(&nb, 0, sizeof(nb)); }

static void nb_collect(wire_src_t *w) {
  for (int b = 0; b < 2; b++) {
    if (nb.state[b] != NB_IN_FLIGHT)
      continue;
    int r = fxio_async_wait(w->idle, w->idle_ctx);
    nb.state[b] = NB_READY;
    nb.len[b] = r;
    if (r <= 0)
      w->srv_pos = -1;
  }
}

/* Start fetching frames from raw offset `off` into buffer b. */
static void nb_start(wire_src_t *w, int b, int64_t off) {
  nb.raw[b] = off;
  nb.parsed[b] = 0;
  nb.state[b] = NB_READY;
  if (w->srv_pos != off) {
    int64_t s = fileXioLseek64(w->fd, off, FIO_SEEK_SET);
    if (s != off) {
      nb.len[b] = s < 0 ? (int)s : -5;
      w->srv_pos = -1;
      return;
    }
  }
  w->srv_pos = -1; /* known again once the frames are parsed */
  if (w->async)
    fileXioSetBlockMode(FXIO_NOWAIT);
  int r = fileXioRead(w->fd, nbuf[b], (int)g_req);
  if (!w->async) {
    nb.len[b] = r;
    return;
  }
  if (r < 0) {
    fileXioSetBlockMode(FXIO_WAIT);
    nb.len[b] = r;
    return;
  }
  nb.state[b] = NB_IN_FLIGHT;
}

/* Parse the next frame of buffer b (its raw offset is the current
 * position) into the view. 0, or <0 on a framing error. */
static int nb_take_frame(wire_src_t *w, int b) {
  wire_frame_t f;
  uint32_t raw_max = wire_frame_raw_for(g_req);
  int t = wire_frame_parse(nbuf[b] + nb.parsed[b], (uint32_t)(nb.len[b] - nb.parsed[b]), raw_max,
                           &f);
  if (t < 0 || nb.raw[b] + f.raw_len > w->size)
    return -5;
  if (f.flags & WIRE_F_LZ4) {
    if (wire_frame_decode(&f, rbuf) < 0)
      return -5;
    nb.view = rbuf;
  } else {
    nb.view = f.data; /* stored: lend it straight from the network buffer */
  }
  nb.view_off = nb.raw[b];
  nb.view_len = f.raw_len;
  nb.parsed[b] += t;
  nb.raw[b] += f.raw_len;
  if (nb.parsed[b] >= nb.len[b]) {
    w->srv_pos = nb.raw[b]; /* the server moved past exactly these frames */
    /* Fetch the next frames while the caller works on this one. */
    int o = 1 - b;
    if (w->async && nb.state[o] != NB_IN_FLIGHT && nb.raw[b] < w->size &&
        !(nb.state[o] == NB_READY && nb.len[o] > 0 && nb.raw[o] == nb.raw[b] && nb.parsed[o] == 0))
      nb_start(w, o, nb.raw[b]);
  }
  return 0;
}

static int nb_unparsed_at(int b, int64_t pos) {
  return nb.state[b] == NB_READY && nb.len[b] > 0 && nb.parsed[b] < nb.len[b] &&
         nb.raw[b] == pos;
}

static int w_read_ptr(GameSource *src, uint32_t max, const uint8_t **out) {
  wire_src_t *w = src->priv;
  if (nb.owner != w) {
    nb_reset();
    nb.owner = w;
  }
  if (w->pos >= w->size)
    return 0;
  for (int tries = 0; tries < 6; tries++) {
    if (nb.view && w->pos >= nb.view_off && w->pos < nb.view_off + nb.view_len) {
      uint32_t avail = (uint32_t)(nb.view_off + nb.view_len - w->pos);
      uint32_t n = avail < max ? avail : max;
      *out = nb.view + (w->pos - nb.view_off);
      w->pos += n;
      return (int)n;
    }
    nb.view = NULL;
    int b = nb_unparsed_at(0, w->pos) ? 0 : nb_unparsed_at(1, w->pos) ? 1 : -1;
    if (b >= 0) {
      int r = nb_take_frame(w, b);
      if (r < 0) {
        nb.state[b] = NB_EMPTY;
        w->srv_pos = -1;
        return r;
      }
      continue;
    }
    for (b = 0; b < 2; b++)
      if (nb.state[b] == NB_READY && nb.raw[b] == w->pos && nb.parsed[b] == 0 && nb.len[b] <= 0) {
        int r = nb.len[b];
        nb.state[b] = NB_EMPTY;
        return r; /* 0: the server ended early */
      }
    if (nb.state[0] == NB_IN_FLIGHT || nb.state[1] == NB_IN_FLIGHT) {
      nb_collect(w);
      continue;
    }
    b = nb.state[0] == NB_EMPTY ? 0 : 1;
    if (nb.state[b] != NB_EMPTY && nb.state[1 - b] == NB_EMPTY)
      b = 1 - b;
    nb_start(w, b, w->pos);
  }
  return -5;
}

static int w_open(GameSource *src, const char *path) {
  wire_src_t *w = src->priv;
  char wp[SOURCE_PATH_MAX];
  if (wire_path(path, wp, sizeof(wp)) < 0)
    return -22;
  w->fd = fileXioOpen(wp, FIO_O_RDONLY);
  if (w->fd < 0)
    return w->fd;
  w->pos = 0;
  w->size = fileXioLseek64(w->fd, 0, FIO_SEEK_END);
  if (w->size <= 0) {
    int rc = w->size < 0 ? (int)w->size : -5;
    fileXioClose(w->fd);
    w->fd = -1;
    return rc;
  }
  w->srv_pos = fileXioLseek64(w->fd, 0, FIO_SEEK_SET) == 0 ? 0 : -1;
  return 0;
}

static void w_set_async(GameSource *src, int mode, void (*idle)(void *ctx), void *ctx) {
  wire_src_t *w = src->priv;
  void (*keep)(void *) = w->idle; /* collect without it (source_udpfs.c) */
  w->idle = NULL;
  if (nb.owner == w)
    nb_collect(w);
  w->idle = keep;
  if (mode == SRC_ASYNC_ON) {
    w->async = 1;
    w->idle = idle;
    w->idle_ctx = ctx;
  } else if (mode == SRC_ASYNC_OFF) {
    w->async = 0;
    w->idle = NULL;
  }
}

static int w_close(GameSource *src) {
  wire_src_t *w = src->priv;
  w_set_async(src, SRC_ASYNC_OFF, NULL, NULL);
  if (nb.owner == w)
    nb_reset();
  int r = 0;
  if (w->fd >= 0)
    r = fileXioClose(w->fd);
  w->fd = -1;
  return r;
}

static int w_read(GameSource *src, void *buf, uint32_t size) {
  const uint8_t *p;
  int n = w_read_ptr(src, size, &p);
  if (n > 0)
    memcpy(buf, p, (size_t)n);
  return n;
}

static int64_t w_seek(GameSource *src, int64_t off, int whence) {
  wire_src_t *w = src->priv;
  int64_t np = whence == SRC_SEEK_CUR   ? w->pos + off
               : whence == SRC_SEEK_END ? w->size + off
                                        : off;
  if (np < 0)
    return -22;
  w->pos = np; /* frames are found by raw offset; others are refetched */
  return np;
}

static int64_t w_size(GameSource *src) { return ((wire_src_t *)src->priv)->size; }

static const GameSourceOps WIRE_OPS = {w_open, w_close,     w_read,    w_seek,
                                       w_size, w_set_async, w_read_ptr};

void source_wire_init(GameSource *src, wire_src_t *w) {
  memset(src, 0, sizeof(*src));
  memset(w, 0, sizeof(*w));
  w->fd = -1;
  w->srv_pos = -1;
  src->ops = &WIRE_OPS;
  src->priv = w;
}
