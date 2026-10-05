#include <stdio.h>
#include <string.h>
#include <time.h>

#include <timer.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>

#include "crc32.h"
#include "hdd_partitions.h"
#include "hdl_install.h"
#include "partname.h"
#include "pump.h"
#include "util.h"

static uint8_t stream_buf[STREAM_BUF_SIZE] __attribute__((aligned(64)));
/* Second buffer for the read-back (read next block while checking one). */
static uint8_t stream_buf2[STREAM_BUF_SIZE] __attribute__((aligned(64)));
stream_timing_t g_stream_timing;

static hdl_result_t res(inst_err_t e, int rc) {
  hdl_result_t r;
  memset(&r, 0, sizeof(r));
  r.err = e;
  r.rc = rc;
  return r;
}

hdl_result_t hdl_create_and_format(const char *hidden, const hdl_alloc_t *alloc,
                                   const struct HDLFS_FormatArgs *args) {
  char dev[48], create[80];
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  snprintf(create, sizeof(create), "%s,,,%s,HDL", dev, alloc->main_size_str);

  int ex = hdd_exists(hidden);
  if (ex != 0)
    return res(ex > 0 ? ERR_PARTITION_EXISTS : ERR_HDL_CREATE, ex);

  /* FIO_O_* (IOP flag values), not newlib O_*: apa-hdl's HIOCADDSUB
   * checks `mode & FIO_O_WRONLY` (ps2-usbhdl fix). */
  int fd = fileXioOpen(create, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0644);
  if (fd < 0)
    return res(ERR_HDL_CREATE, fd);
  for (int i = 0; i < alloc->subs; i++) {
    const char *sz = alloc->sub_size_str[i];
    int r = fileXioIoctl2(fd, HIOCADDSUB, (char *)sz, strlen(sz) + 1, NULL, 0);
    if (r < 0) {
      fileXioClose(fd);
      hdd_remove_exact(hidden, NULL);
      return res(ERR_HDL_CREATE, r);
    }
  }
  fileXioClose(fd);

  int r = fileXioFormat("hdl0:", dev, (const char *)args, sizeof(*args));
  if (r < 0) {
    hdd_remove_exact(hidden, NULL);
    return res(ERR_HDL_FORMAT, r);
  }
  return res(ERR_OK, 0);
}

static void report(const stream_cb_t *cb, time_t start, time_t *last, uint64_t done,
                   uint64_t total, int *abort) {
  time_t now = time(NULL);
  if (!cb || (now == *last && done != total))
    return;
  *last = now;
  if (cb->progress)
    cb->progress(cb->ctx, done, total, (uint32_t)(now - start));
  if (cb->should_abort && cb->should_abort(cb->ctx))
    *abort = 1;
}

int g_hdl_use_pump;

#define PUMP_SLOT (128 * 1024)
#define PUMP_SLOTS 4

/* Checkpoint bookkeeping shared by both copy loops. `safe` is the last
 * position known to be on the HDD with `seg` = CRC-32 of the source bytes
 * since the previous checkpoint `cp` (whose cumulative CRC is cp_crc). */
typedef struct {
  const stream_cb_t *cb;
  uint64_t cp, safe;
  uint32_t cp_crc, seg;
} cp_state_t;

static uint32_t cum_crc(const cp_state_t *c) { return crc32_combine(c->cp_crc, c->seg, c->safe - c->cp); }

/* Save a checkpoint at the safe position (HDD cache flushed first, so the
 * journal never names data that is only in the drive's cache). 0 / -1. */
static int save_checkpoint(cp_state_t *c) {
  if (c->safe <= c->cp || !c->cb || !c->cb->checkpoint)
    return 0;
  fileXioDevctl("hdd0:", HDIOC_FLUSH, NULL, 0, NULL, 0);
  uint32_t cum = cum_crc(c);
  if (c->cb->checkpoint(c->cb->cp_ctx, c->safe, cum, c->seg))
    return -1;
  c->cp = c->safe;
  c->cp_crc = cum;
  c->seg = 0;
  return 0;
}

/* On any stop, the checkpoint also moves to where the copy really got
 * to, so a resume repeats as little as possible. */
static void finish_stream(cp_state_t *c, hdl_result_t *out, uint64_t total) {
  out->bytes = c->safe;
  out->crc32 = cum_crc(c);
  if (out->err == ERR_OK) {
    if (c->safe != total)
      out->err = ERR_HDL_WRITE;
  } else {
    save_checkpoint(c); /* best effort: the last periodic one stays */
  }
}

/* Copy loop with hddpump.irx: the IOP writes block n on its own thread
 * while the EE reads block n+1 from the source (fileXio's IOP thread),
 * so network and HDD work at the same time. hdl0: is already mounted.
 * Returns 1 and fills *out when it ran, 0 if the pump is unavailable
 * (nothing written; the caller uses its own loop). */
static int stream_pumped(GameSource *src, uint64_t total, cp_state_t *c, hdl_result_t *out) {
  uint32_t slot = 0;
  if (!g_hdl_use_pump ||
      pump_begin("hdl0:", (uint32_t)(c->safe / ISO_SECTOR), PUMP_SLOT, PUMP_SLOTS, &slot) < 0)
    return 0;
  if (slot > STREAM_BUF_SIZE)
    slot = STREAM_BUF_SIZE;
  *out = res(ERR_OK, 0);
  uint64_t start = c->safe, queued = c->safe;
  uint32_t qseg = c->seg; /* segment CRC including queued, unflushed blocks */
  if (src->ops->seek(src, (int64_t)start, SRC_SEEK_SET) != (int64_t)start) {
    out->err = ERR_SOURCE_READ;
    pump_end(NULL);
    finish_stream(c, out, total);
    return 1;
  }
  time_t t_start = time(NULL), last = 0;
  int abort = 0;
  memset(&g_stream_timing, 0, sizeof(g_stream_timing));
  while (queued < total) {
    uint32_t want = total - queued > slot ? slot : (uint32_t)(total - queued);
    u64 t0 = GetTimerSystemTime();
    inst_err_t e = source_read_exact(src, stream_buf, want);
    if (e) {
      out->err = e;
      out->rc = src->last_rc;
      break;
    }
    u64 t1 = GetTimerSystemTime();
    uint32_t nseg = crc32_update(qseg, stream_buf, want);
    u64 t2 = GetTimerSystemTime();
    /* Waits only while every IOP buffer is still queued for the HDD. */
    int w = pump_put(stream_buf, want);
    if (w < 0) {
      out->err = ERR_HDL_WRITE;
      out->rc = w;
      break;
    }
    queued += want;
    qseg = nseg;
    g_stream_timing.read_ticks += t1 - t0;
    g_stream_timing.crc_ticks += t2 - t1;
    g_stream_timing.write_ticks += GetTimerSystemTime() - t2; /* HDD wait only */
    g_stream_timing.bytes = queued - start;
    report(c->cb, t_start, &last, queued, total, &abort);
    if (queued >= c->cp + STREAM_CHECKPOINT && queued < total) {
      int f = pump_flush(); /* everything queued is now on the HDD */
      if (f < 0) {
        out->err = ERR_HDL_WRITE;
        out->rc = f;
        break;
      }
      c->safe = queued;
      c->seg = qseg;
      if (save_checkpoint(c) < 0) {
        out->err = ERR_JOURNAL;
        break;
      }
      qseg = c->seg;
    }
    if (abort) {
      out->err = ERR_USER_ABORT;
      break;
    }
  }
  /* Whatever was queued is on the HDD once the flush succeeds. */
  if (pump_flush() == 0) {
    c->safe = queued;
    c->seg = qseg;
  }
  uint64_t wrote = 0;
  int r = pump_end(&wrote);
  if (r < 0 && out->err == ERR_OK) {
    out->err = ERR_HDL_WRITE;
    out->rc = r;
  }
  if (start + wrote < c->safe) { /* defensive: never claim unwritten bytes */
    c->safe = c->cp;
    c->seg = 0;
  }
  finish_stream(c, out, total);
  return 1;
}

hdl_result_t hdl_stream(const char *hidden, GameSource *src, uint64_t total, uint64_t start,
                        uint32_t start_crc, const stream_cb_t *cb) {
  char dev[48];
  hdl_result_t out = res(ERR_OK, 0);
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);

  if (total == 0 || total % ISO_SECTOR || start >= total || start % ISO_SECTOR)
    return res(ERR_INVALID_ARG, 0);
  cp_state_t c = {cb, start, start, start_crc, 0};

  fileXioUmount("hdl0:");
  int r = fileXioMount("hdl0:", dev, FIO_MT_RDWR);
  if (r < 0)
    return res(ERR_HDL_MOUNT, r);
  if (stream_pumped(src, total, &c, &out)) {
    fileXioUmount("hdl0:");
    return out;
  }
  int fd = fileXioOpen("hdl0:", FIO_O_RDWR);
  if (fd < 0) {
    fileXioUmount("hdl0:");
    return res(ERR_HDL_MOUNT, fd);
  }

  /* hdlfs lseek takes 2048-byte sector numbers. */
  if (src->ops->seek(src, (int64_t)start, SRC_SEEK_SET) != (int64_t)start ||
      fileXioLseek(fd, (int)(start / ISO_SECTOR), FIO_SEEK_SET) != (int)(start / ISO_SECTOR)) {
    out.err = start ? ERR_HDL_WRITE : ERR_SOURCE_READ;
    goto done;
  }

  time_t t_start = time(NULL), last = 0;
  int abort = 0;
  memset(&g_stream_timing, 0, sizeof(g_stream_timing));
  while (c.safe < total) {
    uint32_t want = total - c.safe > STREAM_BUF_SIZE ? STREAM_BUF_SIZE
                                                     : (uint32_t)(total - c.safe);
    /* A short read before the expected end is a hard error. */
    u64 t0 = GetTimerSystemTime();
    inst_err_t e = source_read_exact(src, stream_buf, want);
    if (e) {
      out.err = e;
      out.rc = src->last_rc;
      break;
    }
    u64 t1 = GetTimerSystemTime();
    /* The HDD write runs on the IOP while the EE computes the CRC of the
     * same (unchanged) buffer: start it without waiting, CRC, then wait.
     * No other fileXio call is made in between (fileXioWaitAsync needs
     * the non-blocking mode until it has collected the result). */
    fileXioSetBlockMode(FXIO_NOWAIT);
    int w = fileXioWrite(fd, stream_buf, (int)want);
    /* CRC over exactly the bytes received from UDPFS (for a ZSO source:
     * the decompressed ISO stream); kept only once the write succeeded. */
    uint32_t nseg = crc32_update(c.seg, stream_buf, want);
    u64 t2 = GetTimerSystemTime();
    if (w >= 0)
      fileXioWaitAsync(FXIO_WAIT, &w);
    fileXioSetBlockMode(FXIO_WAIT);
    if (w != (int)want) {
      out.err = ERR_HDL_WRITE;
      out.rc = w;
      break;
    }
    c.safe += want;
    c.seg = nseg;
    /* CRC and HDD write overlap: each is timed from t1 on its own, so the
     * time per block is network + the slower of the two. */
    g_stream_timing.read_ticks += t1 - t0;
    g_stream_timing.crc_ticks += t2 - t1;
    g_stream_timing.write_ticks += GetTimerSystemTime() - t1;
    g_stream_timing.bytes = c.safe - start;
    report(cb, t_start, &last, c.safe, total, &abort);
    if (c.safe >= c.cp + STREAM_CHECKPOINT && c.safe < total && save_checkpoint(&c) < 0) {
      out.err = ERR_JOURNAL;
      break;
    }
    if (abort) {
      out.err = ERR_USER_ABORT;
      break;
    }
  }
  finish_stream(&c, &out, total);

done:
  if (fileXioClose(fd) < 0 && out.err == ERR_OK)
    out.err = ERR_HDL_WRITE;
  fileXioUmount("hdl0:");
  return out;
}

int hdl_crc_range(const char *hidden, uint64_t start, uint64_t end, uint32_t *crc) {
  char dev[48];
  if (end <= start || start % ISO_SECTOR || end % ISO_SECTOR)
    return -22;
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  fileXioUmount("hdl0:");
  int r = fileXioMount("hdl0:", dev, FIO_MT_RDONLY);
  if (r < 0)
    return r;
  int fd = fileXioOpen("hdl0:", FIO_O_RDONLY);
  if (fd < 0) {
    fileXioUmount("hdl0:");
    return fd;
  }
  uint32_t c = 0;
  r = fileXioLseek(fd, (int)(start / ISO_SECTOR), FIO_SEEK_SET) == (int)(start / ISO_SECTOR) ? 0 : -5;
  for (uint64_t pos = start; !r && pos < end;) {
    uint32_t want = end - pos > STREAM_BUF_SIZE ? STREAM_BUF_SIZE : (uint32_t)(end - pos);
    int n = fileXioRead(fd, stream_buf, (int)want);
    if (n != (int)want) {
      r = n < 0 ? n : -5;
      break;
    }
    c = crc32_update(c, stream_buf, want);
    pos += want;
  }
  fileXioClose(fd);
  fileXioUmount("hdl0:");
  *crc = c;
  return r;
}

static uint8_t hdr[HDL_HEADER_SIZE] __attribute__((aligned(64)));

static int read_raw_header(const char *hidden) {
  char dev[48];
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  int fd = fileXioOpen(dev, FIO_O_RDONLY);
  if (fd < 0)
    return fd;
  int s = fileXioLseek(fd, HDL_GAME_DATA_OFFSET, FIO_SEEK_SET);
  int r = s == HDL_GAME_DATA_OFFSET ? fileXioRead(fd, hdr, HDL_HEADER_SIZE) : -5;
  fileXioClose(fd);
  return r == HDL_HEADER_SIZE ? 0 : (r < 0 ? r : -5);
}

int hdl_partition_identity(const char *hidden, uint32_t *start, uint32_t *size,
                           uint32_t *header_crc32) {
  uint16_t type = 0;
  int r = hdd_stat(hidden, &type, size, start);
  if (r < 0)
    return r;
  if (type != APA_TYPE_HDL_ID)
    return -22;
  if ((r = read_raw_header(hidden)) < 0)
    return r;
  *header_crc32 = crc32_update(0, hdr, HDL_HEADER_SIZE);
  return 0;
}

int hdl_read_header(const char *hidden, hdl_header_info_t *out) {
  int r = read_raw_header(hidden);
  if (r < 0)
    return r;
  return hdl_header_parse(hdr, out) == 0 ? 0 : -22;
}

int hdl_partition_looks_valid(const char *hidden, hdl_header_info_t *out) {
  uint16_t type = 0;
  if (hdd_stat(hidden, &type, NULL, NULL) < 0 || type != APA_TYPE_HDL_ID)
    return 0;
  if (hdl_read_header(hidden, out) < 0)
    return 0;
  return boot_id_is_valid(out->startup) && out->title[0] && out->data_bytes > 0;
}

static hdl_result_t read_back(const char *hidden, uint64_t total, const iso_info_t *iso,
                              const stream_cb_t *cb);

hdl_result_t hdl_verify(const char *hidden, const iso_info_t *iso,
                        int expected_parts, const stream_cb_t *cb) {
  uint64_t total = (uint64_t)iso->sectors * ISO_SECTOR;
  uint16_t type = 0;
  int r = hdd_stat(hidden, &type, NULL, NULL);
  if (r < 0)
    return res(ERR_HDL_VERIFY, r);
  if (type != APA_TYPE_HDL_ID)
    return res(ERR_HDL_VERIFY, type);

  /* Structural: HDL header fields as written by hdlfs_format. */
  hdl_header_info_t h;
  r = hdl_read_header(hidden, &h);
  if (r < 0)
    return res(ERR_HDL_VERIFY, r);
  inst_err_t e = hdl_header_check(&h, iso->boot_id, iso->disc_type,
                                  iso->layer1_start, expected_parts, total);
  if (e)
    return res(e, 0);
  return read_back(hidden, total, iso, cb);
}

hdl_result_t hdl_read_back(const char *hidden, uint64_t total, const stream_cb_t *cb) {
  uint16_t type = 0;
  int r = hdd_stat(hidden, &type, NULL, NULL);
  if (r < 0)
    return res(ERR_HDL_VERIFY, r);
  if (type != APA_TYPE_HDL_ID)
    return res(ERR_HDL_VERIFY, type);
  if (total == 0 || total % ISO_SECTOR)
    return res(ERR_INVALID_ARG, 0);
  return read_back(hidden, total, NULL, cb);
}

/* Full read-back of the installed data from the HDD, read-only mount.
 * With `iso`, sector 16 must hold its PVD. The caller compares the CRC
 * with the CRC of the source stream. */
static hdl_result_t read_back(const char *hidden, uint64_t total, const iso_info_t *iso,
                              const stream_cb_t *cb) {
  int r;
  char dev[48];
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  fileXioUmount("hdl0:");
  r = fileXioMount("hdl0:", dev, FIO_MT_RDONLY);
  if (r < 0)
    return res(ERR_HDL_VERIFY, r);
  int fd = fileXioOpen("hdl0:", FIO_O_RDONLY);
  if (fd < 0) {
    fileXioUmount("hdl0:");
    return res(ERR_HDL_VERIFY, fd);
  }

  hdl_result_t out = res(ERR_OK, 0);
  /* hdlfs lseek takes 2048-byte sector numbers. */
  r = fileXioLseek(fd, 0, FIO_SEEK_SET);
  if (r != 0) {
    out = res(ERR_HDL_VERIFY, r);
    goto done;
  }
  time_t start = time(NULL), last = 0;
  uint64_t done_bytes = 0;
  uint32_t crc = 0;
  int abort = 0;
  memset(&g_stream_timing, 0, sizeof(g_stream_timing));
  /* Double buffer: the IOP reads block n+1 from the HDD into one buffer
   * while the EE computes the CRC of block n in the other. */
  uint8_t *bufs[2] = {stream_buf, stream_buf2};
  int cur = 0;
  uint32_t want = total > STREAM_BUF_SIZE ? STREAM_BUF_SIZE : (uint32_t)total;
  u64 t0 = GetTimerSystemTime();
  r = fileXioRead(fd, bufs[cur], (int)want);
  while (done_bytes < total) {
    u64 t1 = GetTimerSystemTime();
    g_stream_timing.read_ticks += t1 - t0;
    if (r != (int)want) {
      out = res(ERR_HDL_VERIFY, r);
      break;
    }
    const uint8_t *blk = bufs[cur]; /* the block to check */
    uint64_t after = done_bytes + want;
    uint32_t next = total - after > STREAM_BUF_SIZE ? STREAM_BUF_SIZE
                                                     : (uint32_t)(total - after);
    int pending = 0;
    if (next) {
      fileXioSetBlockMode(FXIO_NOWAIT);
      t0 = GetTimerSystemTime();
      r = fileXioRead(fd, bufs[cur ^ 1], (int)next);
      pending = r >= 0;
      if (!pending)
        fileXioSetBlockMode(FXIO_WAIT);
    }
    /* Structural: the PVD must be at sector 16 of the installed data.
     * A ZSO container (no PVD there) can never pass this. */
    int bad = iso && done_bytes == 0 && want >= 17 * ISO_SECTOR &&
              (memcmp(blk + 16 * ISO_SECTOR, "\x01" "CD001", 6) != 0 ||
               get_u32le(blk + 16 * ISO_SECTOR + 80) != iso->pvd_blocks ||
               memcmp(blk + 16 * ISO_SECTOR + 40, iso->volume_id,
                      strlen(iso->volume_id)) != 0);
    if (!bad) {
      crc = crc32_update(crc, blk, want);
      done_bytes += want;
    }
    u64 t2 = GetTimerSystemTime();
    g_stream_timing.crc_ticks += t2 - t1;
    g_stream_timing.bytes = done_bytes;
    if (!bad)
      report(cb, start, &last, done_bytes, total, &abort);
    /* Collect the read in flight before anything else uses fileXio. */
    if (pending) {
      fileXioWaitAsync(FXIO_WAIT, &r);
      fileXioSetBlockMode(FXIO_WAIT);
    }
    if (bad) {
      out = res(ERR_HDL_VERIFY, 0);
      break;
    }
    if (abort) {
      out = res(ERR_USER_ABORT, 0);
      break;
    }
    if (!next)
      break;
    if (!pending) {
      out = res(ERR_HDL_VERIFY, r);
      break;
    }
    cur ^= 1;
    want = next;
  }
  out.bytes = done_bytes;
  out.crc32 = crc;
done:
  fileXioClose(fd);
  fileXioUmount("hdl0:");
  return out;
}
