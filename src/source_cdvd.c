#include <string.h>

#include "source_cdvd.h"

cdvd_disc_t cdvd_classify(int disk_type) {
  switch (disk_type) {
  case 0x00: /* SCECdNODISC */
  case 0x01: /* SCECdDETCT... still detecting */
  case 0x02:
  case 0x03:
  case 0x04:
    return CDVD_DISC_NONE;
  case 0x10: /* SCECdPSCD */
  case 0x11: /* SCECdPSCDDA */
    return CDVD_DISC_PS1;
  case 0x12: /* SCECdPS2CD */
  case 0x13: /* SCECdPS2CDDA */
    return CDVD_DISC_PS2_CD;
  case 0x14: /* SCECdPS2DVD */
    return CDVD_DISC_PS2_DVD;
  default:
    return CDVD_DISC_OTHER;
  }
}

#ifdef _EE
#include <kernel.h>
#include <libcdvd.h>
#include <sifrpc.h>

#include "ui.h"

#define CD_SERVER_INIT 0x80000592 /* cdvdfsv's init RPC (libcdvd) */

static int g_inited;

/* libcdvd's sceCdInit waits forever for cdvdfsv: make sure it is there. */
static int cdvd_init(void) {
  if (g_inited)
    return 0;
  static SifRpcClientData_t probe __attribute__((aligned(64)));
  memset(&probe, 0, sizeof(probe));
  for (int i = 0; i < 100; i++) {
    if (SifBindRpc(&probe, CD_SERVER_INIT, 0) < 0)
      return -19;
    if (probe.server)
      break;
    nopdelay();
  }
  if (!probe.server)
    return -19;
  if (sceCdInit(SCECdINoD) != 1)
    return -5;
  g_inited = 1;
  return 0;
}

cdvd_disc_t cdvd_disc_detect(int timeout_ms) {
  if (cdvd_init() < 0)
    return CDVD_DISC_NO_DRIVE;
  cdvd_disc_t d = CDVD_DISC_NONE;
  for (int waited = 0; waited <= timeout_ms; waited += 100) {
    int t = sceCdGetDiskType();
    d = cdvd_classify(t);
    /* Detected and spun up: done. Empty tray: no need to wait long. */
    if (d != CDVD_DISC_NONE && sceCdDiskReady(1) == SCECdComplete)
      return d;
    if (t == SCECdNODISC && waited >= 1000)
      return CDVD_DISC_NONE;
    ui_delay_ms(100);
  }
  return d;
}

/* sceCdReadDvdDualInfo through cdvdfsv's S-command RPC (0x80000593,
 * function 39): PS2SDK's EE libcdvd has no wrapper for it. The reply is
 * {result, on_dual, layer1_start}; result 1 = success. 0 or <0. */
#define CD_SERVER_SCMD 0x80000593
#define CD_SCMD_READ_DUAL_INFO 39
int cdvd_dual_info(int *dual, uint32_t *layer1_start) {
  static SifRpcClientData_t cd __attribute__((aligned(64)));
  static int in[4] __attribute__((aligned(64)));
  static struct {
    int result;
    int on_dual;
    unsigned int layer1_start;
    int pad[13];
  } out __attribute__((aligned(64)));
  if (!cd.server) {
    for (int i = 0; i < 100 && !cd.server; i++) {
      if (SifBindRpc(&cd, CD_SERVER_SCMD, 0) < 0)
        return -19;
      if (!cd.server)
        nopdelay();
    }
    if (!cd.server)
      return -19;
  }
  memset(&out, 0, sizeof(out));
  if (SifCallRpc(&cd, CD_SCMD_READ_DUAL_INFO, 0, in, sizeof(in), &out, sizeof(out), NULL,
                 NULL) < 0 ||
      out.result != 1)
    return -5;
  *dual = out.on_dual != 0;
  *layer1_start = out.layer1_start;
  return 0;
}

void cdvd_eject(void) {
  if (cdvd_init() < 0)
    return;
  u32 traycnt = 0;
  sceCdTrayReq(SCECdTrayOpen, &traycnt);
}

/* ---- source -------------------------------------------------------- */

/* Two read-ahead buffers keyed by sector, like source_udpfs. */
enum { CB_EMPTY = 0, CB_IN_FLIGHT, CB_READY };
#define CB_BYTES (CDVD_RA_SECTORS * 2048)
static uint8_t cb_buf[2][CB_BYTES] __attribute__((aligned(64)));
static struct {
  int state[2];
  uint32_t lsn[2];
  uint32_t count[2]; /* sectors */
  int err[2];        /* <0: that read failed */
} cb;

static sceCdRMode g_mode;

static int read_wait(cdvd_src_t *c) {
  while (sceCdSync(1)) {
    if (c && c->async && c->idle)
      c->idle(c->idle_ctx);
  }
  int e = sceCdGetError();
  return e == SCECdErNO ? 0 : -(1000 + e); /* distinct from driver codes */
}

static void cb_collect(cdvd_src_t *c) {
  for (int b = 0; b < 2; b++)
    if (cb.state[b] == CB_IN_FLIGHT) {
      cb.err[b] = read_wait(c);
      cb.state[b] = CB_READY;
    }
}

static void cb_start(cdvd_src_t *c, int b, uint32_t lsn) {
  uint64_t left = ((uint64_t)c->size / 2048) - lsn;
  uint32_t n = left > CDVD_RA_SECTORS ? CDVD_RA_SECTORS : (uint32_t)left;
  cb.lsn[b] = lsn;
  cb.count[b] = n;
  cb.err[b] = 0;
  cb.state[b] = CB_READY;
  if (!sceCdRead(lsn, n, cb_buf[b], &g_mode)) {
    cb.err[b] = -5;
    return;
  }
  cb.state[b] = CB_IN_FLIGHT;
  if (!c->async) {
    cb.err[b] = read_wait(c);
    cb.state[b] = CB_READY;
  }
}

static int cb_holds(int b, uint32_t lsn) {
  return cb.state[b] == CB_READY && !cb.err[b] && lsn >= cb.lsn[b] &&
         lsn < cb.lsn[b] + cb.count[b];
}

static int c_read_ptr(GameSource *src, uint32_t max, const uint8_t **out) {
  cdvd_src_t *c = src->priv;
  if (c->pos >= c->size)
    return 0;
  if (c->pos % 2048)
    return -22; /* the copy loops only ever read whole sectors */
  uint32_t lsn = (uint32_t)(c->pos / 2048);
  for (int tries = 0; tries < 4; tries++) {
    int h = cb_holds(0, lsn) ? 0 : cb_holds(1, lsn) ? 1 : -1;
    if (h >= 0) {
      int o = 1 - h;
      uint32_t end = cb.lsn[h] + cb.count[h];
      if (c->async && cb.state[o] != CB_IN_FLIGHT &&
          !(cb.state[o] == CB_READY && !cb.err[o] && cb.lsn[o] == end) &&
          (uint64_t)end * 2048 < (uint64_t)c->size)
        cb_start(c, o, end);
      uint64_t avail = (uint64_t)(end - lsn) * 2048;
      uint32_t n = avail < max ? (uint32_t)avail : max;
      *out = cb_buf[h] + (uint64_t)(lsn - cb.lsn[h]) * 2048;
      c->pos += n;
      return (int)n;
    }
    for (int b = 0; b < 2; b++)
      if (cb.state[b] == CB_READY && cb.lsn[b] == lsn && cb.err[b]) {
        int r = cb.err[b];
        cb.state[b] = CB_EMPTY;
        return r;
      }
    if (cb.state[0] == CB_IN_FLIGHT || cb.state[1] == CB_IN_FLIGHT) {
      cb_collect(c);
      continue;
    }
    cb_start(c, cb.state[0] == CB_EMPTY ? 0 : 1, lsn);
    if (!c->async)
      continue;
    cb_collect(c);
  }
  return -5;
}

static int read_sector(cdvd_src_t *c, uint32_t lsn, uint8_t *out) {
  (void)c;
  static uint8_t s[2048] __attribute__((aligned(64)));
  if (!sceCdRead(lsn, 1, s, &g_mode))
    return -5;
  int r = read_wait(NULL);
  if (r < 0)
    return r;
  memcpy(out, s, 2048);
  return 0;
}

static int pvd_blocks(const uint8_t *s, uint32_t *blocks) {
  if (s[0] != 1 || memcmp(s + 1, "CD001", 5) != 0)
    return -1;
  *blocks = (uint32_t)s[80] | (uint32_t)s[81] << 8 | (uint32_t)s[82] << 16 |
            (uint32_t)s[83] << 24;
  return *blocks > 16 ? 0 : -1;
}

static int c_open(GameSource *src, const char *path) {
  cdvd_src_t *c = src->priv;
  (void)path;
  memset(&cb, 0, sizeof(cb));
  cdvd_disc_t d = cdvd_disc_detect(10000);
  if (d == CDVD_DISC_NO_DRIVE)
    return -19;
  if (d != CDVD_DISC_PS2_CD && d != CDVD_DISC_PS2_DVD)
    return -2; /* nothing installable in the drive */
  c->dvd = d == CDVD_DISC_PS2_DVD;
  memset(&g_mode, 0, sizeof(g_mode));
  g_mode.trycount = 16;
  g_mode.spindlctrl = SCECdSpinNom; /* steady speed: fewer read errors */
  g_mode.datapattern = SCECdSecS2048;
  static uint8_t s[2048];
  uint32_t blocks = 0;
  int r = read_sector(c, 16, s);
  if (r < 0)
    return r;
  if (pvd_blocks(s, &blocks) < 0)
    return -22;
  uint64_t sectors = blocks;
  if (c->dvd) {
    /* DVD-9: the drive reads both layers as one run of sectors. The
     * drive says whether the disc has two layers and where layer 1
     * starts; layer 1 has its own PVD 16 sectors into it, which must sit
     * right at the end of layer 0's volume (the layout iso9660.c and OPL
     * expect of a DVD-9 image). Anything unclear refuses the disc: a
     * DVD-9 copied as one layer would still pass its CRC check. */
    int dual = 0;
    uint32_t l1 = 0, b1 = 0;
    if (cdvd_dual_info(&dual, &l1) < 0)
      return -5;
    if (dual) {
      if (l1 + 16 != blocks)
        return CDVD_ERR_LAYERS;
      if ((r = read_sector(c, l1 + 16, s)) < 0)
        return r;
      if (pvd_blocks(s, &b1) < 0)
        return CDVD_ERR_LAYERS;
      sectors = (uint64_t)l1 + b1;
    }
  }
  c->size = (int64_t)sectors * 2048;
  c->pos = 0;
  return 0;
}

static void c_set_async(GameSource *src, int mode, void (*idle)(void *ctx), void *ctx) {
  cdvd_src_t *c = src->priv;
  void (*keep)(void *) = c->idle; /* collect without it (source_udpfs.c) */
  c->idle = NULL;
  cb_collect(c);
  c->idle = keep;
  if (mode == SRC_ASYNC_ON) {
    c->async = 1;
    c->idle = idle;
    c->idle_ctx = ctx;
  } else if (mode == SRC_ASYNC_OFF) {
    c->async = 0;
    c->idle = NULL;
  }
}

static int c_close(GameSource *src) {
  c_set_async(src, SRC_ASYNC_OFF, NULL, NULL);
  memset(&cb, 0, sizeof(cb));
  sceCdStop();
  sceCdSync(0);
  return 0;
}

static int c_read(GameSource *src, void *buf, uint32_t size) {
  cdvd_src_t *c = src->priv;
  if (c->pos % 2048) {
    /* Unaligned (ISO probe of a directory record): through a sector. */
    static uint8_t s[2048] __attribute__((aligned(64)));
    if (c->pos >= c->size)
      return 0;
    uint32_t off = (uint32_t)(c->pos % 2048);
    int r = read_sector(c, (uint32_t)(c->pos / 2048), s);
    if (r < 0)
      return r;
    uint32_t n = 2048 - off < size ? 2048 - off : size;
    memcpy(buf, s + off, n);
    c->pos += n;
    return (int)n;
  }
  const uint8_t *p;
  int n = c_read_ptr(src, size, &p);
  if (n > 0)
    memcpy(buf, p, (size_t)n);
  return n;
}

static int64_t c_seek(GameSource *src, int64_t off, int whence) {
  cdvd_src_t *c = src->priv;
  int64_t np = whence == SRC_SEEK_CUR   ? c->pos + off
               : whence == SRC_SEEK_END ? c->size + off
                                        : off;
  if (np < 0)
    return -22;
  c->pos = np;
  return np;
}

static int64_t c_size(GameSource *src) { return ((cdvd_src_t *)src->priv)->size; }

static const GameSourceOps CDVD_OPS = {c_open, c_close,     c_read,    c_seek,
                                       c_size, c_set_async, c_read_ptr};

void source_cdvd_init(GameSource *src, cdvd_src_t *c) {
  memset(src, 0, sizeof(*src));
  memset(c, 0, sizeof(*c));
  src->ops = &CDVD_OPS;
  src->priv = c;
}
#endif
