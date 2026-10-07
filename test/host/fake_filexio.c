/* Fake fileXio for the network source tests (fake/fileXio_rpc.h). */
#include <string.h>

#include "fake/fileXio_rpc.h"
#include "wire_frame.h"

int fake_fxio_violations, fake_fxio_reads, fake_fxio_fail_read, fake_fxio_short_read;
int fake_fxio_wire_lz4;
uint32_t fake_fxio_max_frame;

static uint64_t f_size, f_pos;
static int f_wire, f_mode, f_pending, f_polls, f_result;

uint8_t fake_byte(uint64_t off) { return (uint8_t)((off * 2654435761u) >> 13 ^ off); }

void fake_fxio_reset(uint64_t size, int wire_view) {
  f_size = size;
  f_pos = 0;
  f_wire = wire_view;
  f_mode = FXIO_WAIT;
  f_pending = 0;
  fake_fxio_violations = fake_fxio_reads = fake_fxio_fail_read = fake_fxio_short_read = 0;
  fake_fxio_max_frame = 0;
  fake_fxio_wire_lz4 = 0;
}

static void busy(void) {
  if (f_pending)
    fake_fxio_violations++;
}

int fileXioOpen(const char *path, int flags, ...) {
  (void)path;
  (void)flags;
  busy();
  f_pos = 0;
  return 3;
}

int fileXioClose(int fd) {
  (void)fd;
  busy();
  return 0;
}

int64_t fileXioLseek64(int fd, int64_t off, int whence) {
  busy();
  if (fd != 3) /* another file (journal): not the image's position */
    return off;
  int64_t np = whence == 2 ? (int64_t)f_size + off : whence == 1 ? (int64_t)f_pos + off : off;
  if (np < 0)
    return -22;
  f_pos = (uint64_t)np;
  return np;
}

int fileXioSetBlockMode(int mode) {
  f_mode = mode;
  return 0;
}

/* One read as the server answers it. */
static int do_read(uint8_t *buf, int size) {
  fake_fxio_reads++;
  if (fake_fxio_fail_read && fake_fxio_reads == fake_fxio_fail_read)
    return -5;
  if (!f_wire) {
    uint64_t left = f_pos < f_size ? f_size - f_pos : 0;
    uint32_t n = (uint64_t)size < left ? (uint32_t)size : (uint32_t)left;
    if (fake_fxio_short_read && fake_fxio_reads == fake_fxio_short_read)
      n /= 2;
    for (uint32_t i = 0; i < n; i++)
      buf[i] = fake_byte(f_pos + i);
    f_pos += n;
    return (int)n;
  }
  /* udpfsd's /.lz4f view: frames of the next raw bytes, stored. */
  uint32_t raw = wire_frame_raw_for((uint32_t)size);
  if (fake_fxio_max_frame && raw > fake_fxio_max_frame)
    raw = fake_fxio_max_frame;
  if (f_pos >= f_size)
    return 0;
  if (raw > f_size - f_pos)
    raw = (uint32_t)(f_size - f_pos);
  static uint8_t tmp[256 * 1024];
  for (uint32_t i = 0; i < raw; i++)
    tmp[i] = fake_byte(f_pos + i);
  uint32_t t;
  if (fake_fxio_wire_lz4) {
    /* One LZ4 block of literals only: a valid LZ4 frame whose data
     * length differs from its raw length. */
    uint8_t *f = buf, *d = buf + WIRE_HDR;
    uint32_t k = 0, n = raw;
    d[k++] = (uint8_t)((n >= 15 ? 15 : n) << 4);
    if (n >= 15) {
      uint32_t rem = n - 15;
      for (; rem >= 255; rem -= 255)
        d[k++] = 255;
      d[k++] = (uint8_t)rem;
    }
    memcpy(d + k, tmp, n);
    k += n;
    uint32_t pad = (k + 15u) & ~15u;
    memset(d + k, 0, pad - k);
    uint32_t hdr[4] = {WIRE_MAGIC, raw, k, WIRE_F_LZ4};
    for (int i = 0; i < 4; i++)
      for (int b = 0; b < 4; b++)
        f[i * 4 + b] = (uint8_t)(hdr[i] >> (8 * b));
    t = WIRE_HDR + pad;
    if (t > (uint32_t)size) /* literals cost a little more than raw: store */
      t = wire_frame_build_stored(tmp, raw, buf);
  } else {
    t = wire_frame_build_stored(tmp, raw, buf);
  }
  f_pos += raw;
  /* A transfer cut short (UDPFS reports it as fewer bytes): the server
   * already moved on, the client must notice the broken frame. */
  if (fake_fxio_short_read && fake_fxio_reads == fake_fxio_short_read)
    t /= 2;
  return (int)t;
}

/* A NOWAIT read lands in the caller's buffer only when it completes; until
 * then the buffer holds junk, so reading it early shows up as bad data. */
static uint8_t stage[1024 * 1024];
static uint8_t *f_dst;

int fileXioRead(int fd, void *buf, int size) {
  (void)fd;
  busy();
  if (f_mode == FXIO_NOWAIT) {
    f_result = do_read(stage, size);
    f_dst = buf;
    memset(buf, 0xEE, (size_t)size);
    f_pending = 1;
    f_polls = 2;
    return 0;
  }
  return do_read(buf, size);
}

int fileXioWaitAsync(int mode, int *ret) {
  /* The real one returns 0 (FXIO_INCOMPLETE) without waiting unless the
   * block mode is NOWAIT: a caller that polls in that state loops for
   * ever. Recorded as a violation (and completed, so the test ends). */
  if (f_mode != FXIO_NOWAIT)
    fake_fxio_violations++;
  if (!f_pending)
    return FXIO_COMPLETE;
  if (mode == FXIO_NOWAIT && f_polls-- > 0)
    return FXIO_INCOMPLETE;
  f_pending = 0;
  if (f_result > 0)
    memcpy(f_dst, stage, (size_t)f_result);
  if (ret)
    *ret = f_result;
  return FXIO_COMPLETE;
}
