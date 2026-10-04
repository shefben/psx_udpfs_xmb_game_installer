#include <string.h>

#include "apa_osd_header.h"
#include "util.h"

inst_err_t ppaa_apply(uint8_t *region, size_t region_len, const char *syscnf,
                      size_t len) {
  if (region_len < PPAA_REGION_LEN || len == 0 || len > PPAA_SYSCNF_MAX)
    return ERR_INVALID_ARG;
  memcpy(region, PPAA_MAGIC, PPAA_MAGIC_LEN);
  put_u32le(region + PPAA_SYSCNF_DESC, PPAA_SYSCNF_OFF);
  put_u32le(region + PPAA_SYSCNF_DESC + 4, (uint32_t)len);
  memcpy(region + PPAA_SYSCNF_OFF, syscnf, len);
  memset(region + PPAA_SYSCNF_OFF + len, 0, PPAA_SYSCNF_MAX - len);
  return ERR_OK;
}

inst_err_t ppaa_verify(const uint8_t *region, size_t region_len,
                       const char *syscnf, size_t len) {
  if (region_len < PPAA_REGION_LEN || len == 0 || len > PPAA_SYSCNF_MAX)
    return ERR_XMB_VERIFY;
  if (memcmp(region, PPAA_MAGIC, PPAA_MAGIC_LEN) != 0 ||
      get_u32le(region + PPAA_SYSCNF_DESC) != PPAA_SYSCNF_OFF ||
      get_u32le(region + PPAA_SYSCNF_DESC + 4) != len ||
      memcmp(region + PPAA_SYSCNF_OFF, syscnf, len) != 0)
    return ERR_XMB_VERIFY;
  return ERR_OK;
}

int ppaa_read_syscnf(const uint8_t *region, size_t region_len, char *out,
                     size_t outsz) {
  if (region_len < PPAA_REGION_LEN || outsz == 0 ||
      memcmp(region, PPAA_MAGIC, PPAA_MAGIC_LEN) != 0)
    return -1;
  uint32_t off = get_u32le(region + PPAA_SYSCNF_DESC);
  uint32_t len = get_u32le(region + PPAA_SYSCNF_DESC + 4);
  if (off != PPAA_SYSCNF_OFF || len == 0 || len > PPAA_SYSCNF_MAX ||
      len >= outsz)
    return -1;
  memcpy(out, region + off, len);
  out[len] = 0;
  return (int)len;
}

#ifdef _EE
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <stdio.h>

static uint8_t hdr_buf[PPAA_REGION_LEN] __attribute__((aligned(64)));

/* Raw partition I/O: the APA driver requires 512-byte multiples and
 * maps fd offset 0 to partition + 0x1000 (the PPAA area). */
static int read_region(const char *partition, int *rc_out) {
  char path[48];
  snprintf(path, sizeof(path), "hdd0:%s", partition);
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0) {
    *rc_out = fd;
    return -1;
  }
  int r = fileXioRead(fd, hdr_buf, PPAA_REGION_LEN);
  fileXioClose(fd);
  if (r != PPAA_REGION_LEN) {
    *rc_out = r;
    return -1;
  }
  return 0;
}

inst_err_t ppaa_verify_partition(const char *partition, const char *syscnf,
                                 size_t len, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  if (read_region(partition, rc_out) < 0)
    return ERR_XMB_VERIFY;
  return ppaa_verify(hdr_buf, sizeof(hdr_buf), syscnf, len);
}

inst_err_t ppaa_write_partition(const char *partition, const char *syscnf,
                                size_t len, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  *rc_out = 0;
  if (read_region(partition, rc_out) < 0)
    return ERR_XMB_HEADER_WRITE;
  if (ppaa_apply(hdr_buf, sizeof(hdr_buf), syscnf, len) != ERR_OK)
    return ERR_XMB_HEADER_WRITE;

  char path[48];
  snprintf(path, sizeof(path), "hdd0:%s", partition);
  int fd = fileXioOpen(path, FIO_O_RDWR);
  if (fd < 0) {
    *rc_out = fd;
    return ERR_XMB_HEADER_WRITE;
  }
  int w = fileXioWrite(fd, hdr_buf, PPAA_REGION_LEN);
  fileXioClose(fd);
  if (w != PPAA_REGION_LEN) {
    *rc_out = w;
    return ERR_XMB_HEADER_WRITE;
  }
  /* Byte-for-byte read-back. */
  memset(hdr_buf, 0, sizeof(hdr_buf));
  return ppaa_verify_partition(partition, syscnf, len, rc_out);
}
#endif
