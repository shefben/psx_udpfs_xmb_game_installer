#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>

#include "crc32.h"
#include "hdd_partitions.h"
#include "hdl_install.h"
#include "partname.h"
#include "util.h"

static uint8_t stream_buf[STREAM_BUF_SIZE] __attribute__((aligned(64)));

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

hdl_result_t hdl_stream(const char *hidden, GameSource *src, uint64_t total,
                        const stream_cb_t *cb) {
  char dev[48];
  hdl_result_t out = res(ERR_OK, 0);
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
  uint32_t crc = 0;
  int abort = 0;
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
    /* CRC over exactly the bytes received from UDPFS (for a ZSO source:
     * the decompressed ISO stream). */
    crc = crc32_update(crc, stream_buf, want);
    int w = fileXioWrite(fd, stream_buf, (int)want);
    if (w != (int)want) {
      out.err = ERR_HDL_WRITE;
      out.rc = w;
      break;
    }
    written += want;
    report(cb, start, &last, written, total, &abort);
    if (abort) {
      out.err = ERR_USER_ABORT;
      break;
    }
  }
  out.bytes = written;
  out.crc32 = crc;
  if (out.err == ERR_OK && written != total)
    out.err = ERR_HDL_WRITE;

done:
  if (fileXioClose(fd) < 0 && out.err == ERR_OK)
    out.err = ERR_HDL_WRITE;
  fileXioUmount("hdl0:");
  return out;
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

  /* Full read-back of the installed data from the HDD, read-only mount.
   * The caller compares the CRC with the CRC of the source stream. */
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
  while (done_bytes < total) {
    uint32_t want = total - done_bytes > STREAM_BUF_SIZE
                        ? STREAM_BUF_SIZE
                        : (uint32_t)(total - done_bytes);
    r = fileXioRead(fd, stream_buf, (int)want);
    if (r != (int)want) {
      out = res(ERR_HDL_VERIFY, r);
      break;
    }
    /* Structural: the PVD must be at sector 16 of the installed data.
     * A ZSO container (no PVD there) can never pass this. */
    if (done_bytes == 0 && want >= 17 * ISO_SECTOR &&
        (memcmp(stream_buf + 16 * ISO_SECTOR, "\x01" "CD001", 6) != 0 ||
         get_u32le(stream_buf + 16 * ISO_SECTOR + 80) != iso->pvd_blocks ||
         memcmp(stream_buf + 16 * ISO_SECTOR + 40, iso->volume_id,
                strlen(iso->volume_id)) != 0)) {
      out = res(ERR_HDL_VERIFY, 0);
      break;
    }
    crc = crc32_update(crc, stream_buf, want);
    done_bytes += want;
    report(cb, start, &last, done_bytes, total, &abort);
    if (abort) {
      out = res(ERR_USER_ABORT, 0);
      break;
    }
  }
  out.bytes = done_bytes;
  out.crc32 = crc;
done:
  fileXioClose(fd);
  fileXioUmount("hdl0:");
  return out;
}
