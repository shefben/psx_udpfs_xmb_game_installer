#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>

#include "hdd_partitions.h"
#include "hdl_install.h"
#include "partname.h"
#include "util.h"

static uint8_t stream_buf[STREAM_BUF_SIZE] __attribute__((aligned(64)));
static uint8_t sec_a[ISO_SECTOR] __attribute__((aligned(64)));
static uint8_t sec_b[ISO_SECTOR] __attribute__((aligned(64)));

static hdl_result_t res(inst_err_t e, int rc) {
  hdl_result_t r = {e, rc, 0};
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
      fileXioRemove(dev);
      return res(ERR_HDL_CREATE, r);
    }
  }
  fileXioClose(fd);

  int r = fileXioFormat("hdl0:", dev, (const char *)args, sizeof(*args));
  if (r < 0) {
    fileXioRemove(dev);
    return res(ERR_HDL_FORMAT, r);
  }
  return res(ERR_OK, 0);
}

hdl_result_t hdl_stream(const char *hidden, GameSource *src, uint64_t total,
                        const stream_cb_t *cb) {
  char dev[48];
  hdl_result_t out = {ERR_OK, 0, 0};
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);

  if (total == 0 || total % ISO_SECTOR)
    return res(ERR_INVALID_ARG, 0);

  fileXioUmount("hdl0:");
  int r = fileXioMount("hdl0:", dev, FIO_MT_RDWR);
  if (r < 0)
    return res(ERR_HDL_MOUNT, r);
  int fd = fileXioOpen("hdl0:", FIO_O_RDWR);
  if (fd < 0) {
    fileXioUmount("hdl0:");
    return res(ERR_HDL_MOUNT, fd);
  }

  if (src->ops->seek(src, 0, SRC_SEEK_SET) != 0) {
    out.err = ERR_SOURCE_READ;
    goto done;
  }

  time_t start = time(NULL), last = 0;
  uint64_t written = 0;
  while (written < total) {
    uint32_t want = total - written > STREAM_BUF_SIZE ? STREAM_BUF_SIZE
                                                      : (uint32_t)(total - written);
    /* A short read before the expected end is a hard error. */
    inst_err_t e = source_read_exact(src, stream_buf, want);
    if (e) {
      out.err = e;
      out.rc = src->last_rc;
      break;
    }
    int w = fileXioWrite(fd, stream_buf, (int)want);
    if (w != (int)want) {
      out.err = ERR_HDL_WRITE;
      out.rc = w;
      break;
    }
    written += want;

    time_t now = time(NULL);
    if (cb && (now != last || written == total)) {
      last = now;
      if (cb->progress)
        cb->progress(cb->ctx, written, total, (uint32_t)(now - start));
      if (cb->should_abort && cb->should_abort(cb->ctx)) {
        out.err = ERR_USER_ABORT;
        break;
      }
    }
  }
  out.written = written;
  if (out.err == ERR_OK && written != total)
    out.err = ERR_HDL_WRITE;

done:
  if (fileXioClose(fd) < 0 && out.err == ERR_OK)
    out.err = ERR_HDL_WRITE;
  fileXioUmount("hdl0:");
  return out;
}

int hdl_read_header(const char *hidden, hdl_header_info_t *out) {
  static uint8_t hdr[HDL_HEADER_SIZE] __attribute__((aligned(64)));
  char dev[48];
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  int fd = fileXioOpen(dev, FIO_O_RDONLY);
  if (fd < 0)
    return fd;
  int s = fileXioLseek(fd, HDL_GAME_DATA_OFFSET, FIO_SEEK_SET);
  int r = s == HDL_GAME_DATA_OFFSET ? fileXioRead(fd, hdr, HDL_HEADER_SIZE) : -5;
  fileXioClose(fd);
  if (r != HDL_HEADER_SIZE)
    return r < 0 ? r : -5;
  return hdl_header_parse(hdr, out) == 0 ? 0 : -22;
}

inst_err_t hdl_write_marker(const char *hidden, uint16_t marker,
                            inst_err_t fail_err, int *rc_out) {
  static uint8_t sec[512] __attribute__((aligned(64)));
  char dev[48];
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  /* Raw hdd0: I/O is in 512-byte units; the header's first sector
   * holds the marker at offset 4. */
  int fd = fileXioOpen(dev, FIO_O_RDWR);
  if (fd < 0) {
    *rc_out = fd;
    return fail_err;
  }
  int r = fileXioLseek(fd, HDL_GAME_DATA_OFFSET, FIO_SEEK_SET);
  if (r == HDL_GAME_DATA_OFFSET)
    r = fileXioRead(fd, sec, sizeof(sec));
  if (r == (int)sizeof(sec) && get_u32le(sec) == HDL_INFO_MAGIC) {
    hdl_header_set_marker(sec, marker);
    r = fileXioLseek(fd, HDL_GAME_DATA_OFFSET, FIO_SEEK_SET);
    if (r == HDL_GAME_DATA_OFFSET)
      r = fileXioWrite(fd, sec, sizeof(sec));
  } else if (r == (int)sizeof(sec)) {
    r = -22; /* no HDL header: refuse to write */
  }
  fileXioClose(fd);
  if (r != (int)sizeof(sec)) {
    *rc_out = r;
    return fail_err;
  }
  hdl_header_info_t h;
  r = hdl_read_header(hidden, &h);
  if (r < 0 || h.marker != marker) {
    *rc_out = r;
    return fail_err;
  }
  return ERR_OK;
}

int hdl_partition_looks_valid(const char *hidden, hdl_header_info_t *out) {
  uint16_t type = 0;
  if (hdd_stat(hidden, &type, NULL, NULL) < 0 || type != APA_TYPE_HDL_)
    return 0;
  if (hdl_read_header(hidden, out) < 0)
    return 0;
  return boot_id_is_valid(out->startup) && out->title[0] && out->data_bytes > 0;
}

/* Read one 2 KiB sector of installed data through hdl0:. */
static int hdl_read_sector(int fd, uint32_t lba, void *buf) {
  /* hdlfs lseek takes and returns 2048-byte sector numbers. */
  int s = fileXioLseek(fd, (int)lba, FIO_SEEK_SET);
  if (s != (int)lba)
    return s < 0 ? s : -5;
  int r = fileXioRead(fd, buf, ISO_SECTOR);
  return r == ISO_SECTOR ? 0 : (r < 0 ? r : -5);
}

hdl_result_t hdl_verify(const char *hidden, GameSource *src,
                        const iso_info_t *iso, int expected_parts) {
  uint16_t type = 0;
  int r = hdd_stat(hidden, &type, NULL, NULL);
  if (r < 0)
    return res(ERR_HDL_VERIFY, r);
  if (type != APA_TYPE_HDL_)
    return res(ERR_HDL_VERIFY, type);

  hdl_header_info_t h;
  r = hdl_read_header(hidden, &h);
  if (r < 0)
    return res(ERR_HDL_VERIFY, r);
  inst_err_t e = hdl_header_check(&h, iso->boot_id, iso->disc_type,
                                  iso->layer1_start, expected_parts,
                                  (uint64_t)iso->sectors * ISO_SECTOR);
  if (e)
    return res(e, 0);

  /* Data spot check: the PVD and the final sector must match the
   * source. This also proves the logical ISO (not ZSO container bytes)
   * was written, since a ZSO file has no PVD at sector 16. */
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
  uint32_t probes[2] = {16, iso->sectors - 1};
  e = ERR_OK;
  for (int i = 0; i < 2 && !e; i++) {
    r = hdl_read_sector(fd, probes[i], sec_a);
    if (r < 0) {
      e = ERR_HDL_VERIFY;
      break;
    }
    if (source_read_at(src, (uint64_t)probes[i] * ISO_SECTOR, sec_b, ISO_SECTOR)) {
      e = ERR_SOURCE_READ;
      r = src->last_rc;
      break;
    }
    if (memcmp(sec_a, sec_b, ISO_SECTOR) != 0)
      e = ERR_HDL_VERIFY;
  }
  fileXioClose(fd);
  fileXioUmount("hdl0:");
  return res(e, e ? r : 0);
}
