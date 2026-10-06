#include <string.h>

#include "apa_osd_header.h"
#include "util.h"

static size_t round512(size_t n) { return (n + 511) & ~(size_t)511; }

size_t ppaa_files_span(const ppaa_files_t *f) {
  if (!f->syscnf || f->syscnf_len == 0 || f->syscnf_len > PPAA_SYSCNF_MAX)
    return 0;
  if (f->iconsys && (f->iconsys_len == 0 || f->iconsys_len > PPAA_ICONSYS_MAX))
    return 0;
  if (f->icon && (f->icon_len == 0 || f->icon_len > PPAA_ICON_MAX))
    return 0;
  if (f->kelf && (f->kelf_len < 1024 || f->kelf_len > PPAA_KELF_MAX))
    return 0;
  if (f->icon)
    return (f->del_copy ? PPAA_DELICON_OFF : PPAA_ICON_OFF) + round512(f->icon_len);
  return f->iconsys ? PPAA_ICON_OFF : PPAA_REGION_LEN;
}

/* Descriptor + data + zeroed slot tail. */
static void put_file(uint8_t *region, size_t desc, uint32_t off, const void *data, size_t len,
                     size_t slot_end) {
  put_u32le(region + desc, off);
  put_u32le(region + desc + 4, (uint32_t)len);
  memcpy(region + off, data, len);
  memset(region + off + len, 0, slot_end - off - len);
}

inst_err_t ppaa_apply_files(uint8_t *region, size_t region_len, const ppaa_files_t *f) {
  size_t span = ppaa_files_span(f);
  if (!span || region_len < span)
    return ERR_INVALID_ARG;
  memcpy(region, PPAA_MAGIC, PPAA_MAGIC_LEN);
  put_file(region, PPAA_SYSCNF_DESC, PPAA_SYSCNF_OFF, f->syscnf, f->syscnf_len,
           PPAA_SYSCNF_OFF + PPAA_SYSCNF_MAX);
  if (f->iconsys)
    put_file(region, PPAA_ICONSYS_DESC, PPAA_ICONSYS_OFF, f->iconsys, f->iconsys_len,
             PPAA_ICONSYS_OFF + PPAA_ICONSYS_MAX);
  if (f->icon && f->del_copy) {
    /* hdl_dump layout (PFS-BatchKit-Manager): list icon at 0x800, its
     * copy as delete icon at 0x40000, zeros between. */
    put_file(region, PPAA_ICON_DESC, PPAA_ICON_OFF, f->icon, f->icon_len, PPAA_DELICON_OFF);
    put_file(region, PPAA_DELICON_DESC, PPAA_DELICON_OFF, f->icon, f->icon_len, span);
  } else if (f->icon) {
    put_file(region, PPAA_ICON_DESC, PPAA_ICON_OFF, f->icon, f->icon_len, span);
    /* No separate delete icon: the list icon again (HDLGameInstaller). */
    put_u32le(region + PPAA_DELICON_DESC, PPAA_ICON_OFF);
    put_u32le(region + PPAA_DELICON_DESC + 4, (uint32_t)f->icon_len);
  }
  if (f->kelf) {
    put_u32le(region + PPAA_KELF_DESC, PPAA_KELF_OFF);
    put_u32le(region + PPAA_KELF_DESC + 4, (uint32_t)f->kelf_len);
  }
  return ERR_OK;
}

static int file_ok(const uint8_t *region, size_t desc, uint32_t off, const void *data,
                   size_t len) {
  return get_u32le(region + desc) == off && get_u32le(region + desc + 4) == len &&
         memcmp(region + off, data, len) == 0;
}

inst_err_t ppaa_verify_files(const uint8_t *region, size_t region_len, const ppaa_files_t *f) {
  size_t span = ppaa_files_span(f);
  if (!span || region_len < span || memcmp(region, PPAA_MAGIC, PPAA_MAGIC_LEN) != 0 ||
      !file_ok(region, PPAA_SYSCNF_DESC, PPAA_SYSCNF_OFF, f->syscnf, f->syscnf_len))
    return ERR_XMB_VERIFY;
  if (f->iconsys &&
      !file_ok(region, PPAA_ICONSYS_DESC, PPAA_ICONSYS_OFF, f->iconsys, f->iconsys_len))
    return ERR_XMB_VERIFY;
  if (f->icon && !file_ok(region, PPAA_ICON_DESC, PPAA_ICON_OFF, f->icon, f->icon_len))
    return ERR_XMB_VERIFY;
  if (f->icon && f->del_copy &&
      !file_ok(region, PPAA_DELICON_DESC, PPAA_DELICON_OFF, f->icon, f->icon_len))
    return ERR_XMB_VERIFY;
  if (f->icon && !f->del_copy &&
      (get_u32le(region + PPAA_DELICON_DESC) != PPAA_ICON_OFF ||
       get_u32le(region + PPAA_DELICON_DESC + 4) != f->icon_len))
    return ERR_XMB_VERIFY;
  if (f->kelf && (get_u32le(region + PPAA_KELF_DESC) != PPAA_KELF_OFF ||
                  get_u32le(region + PPAA_KELF_DESC + 4) != f->kelf_len))
    return ERR_XMB_VERIFY;
  return ERR_OK;
}

inst_err_t ppaa_apply(uint8_t *region, size_t region_len, const char *syscnf,
                      size_t len) {
  ppaa_files_t f = {syscnf, len, NULL, 0, NULL, 0, 0, NULL, 0};
  return ppaa_apply_files(region, region_len, &f);
}

inst_err_t ppaa_verify(const uint8_t *region, size_t region_len,
                       const char *syscnf, size_t len) {
  ppaa_files_t f = {syscnf, len, NULL, 0, NULL, 0, 0, NULL, 0};
  return ppaa_verify_files(region, region_len, &f);
}

int ppaa_has_icons(const uint8_t *region, size_t region_len) {
  if (region_len < PPAA_REGION_LEN || memcmp(region, PPAA_MAGIC, PPAA_MAGIC_LEN) != 0)
    return 0;
  uint32_t so = get_u32le(region + PPAA_ICONSYS_DESC), sl = get_u32le(region + PPAA_ICONSYS_DESC + 4);
  uint32_t io = get_u32le(region + PPAA_ICON_DESC), il = get_u32le(region + PPAA_ICON_DESC + 4);
  return so == PPAA_ICONSYS_OFF && sl > 0 && sl <= PPAA_ICONSYS_MAX && io >= PPAA_ICON_OFF &&
         il > 0;
}

int ppaa_has_kelf(const uint8_t *region, size_t region_len) {
  if (region_len < PPAA_REGION_LEN || memcmp(region, PPAA_MAGIC, PPAA_MAGIC_LEN) != 0)
    return 0;
  uint32_t ko = get_u32le(region + PPAA_KELF_DESC), kl = get_u32le(region + PPAA_KELF_DESC + 4);
  return ko == PPAA_KELF_OFF && kl >= 1024 && kl <= PPAA_KELF_MAX;
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

int ppaa_read_iconsys(const uint8_t *region, size_t region_len, char *out, size_t outsz) {
  if (region_len < PPAA_ICON_OFF || outsz == 0 || !ppaa_has_icons(region, region_len))
    return -1;
  uint32_t len = get_u32le(region + PPAA_ICONSYS_DESC + 4);
  if (len >= outsz)
    return -1;
  memcpy(out, region + PPAA_ICONSYS_OFF, len);
  out[len] = 0;
  return (int)len;
}

#ifdef _EE
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <malloc.h>
#include <stdio.h>

static uint8_t hdr_buf[PPAA_OSD_MAX] __attribute__((aligned(64)));

/* Raw partition I/O: the APA driver requires 512-byte multiples and
 * maps fd offset 0 to partition + 0x1000 (the PPAA area). */
static int raw_io(const char *partition, int write, uint32_t off, void *buf, size_t len,
                  int *rc_out) {
  char path[48];
  snprintf(path, sizeof(path), "hdd0:%s", partition);
  int fd = fileXioOpen(path, write ? FIO_O_RDWR : FIO_O_RDONLY);
  if (fd < 0) {
    *rc_out = fd;
    return -1;
  }
  int r = off ? fileXioLseek(fd, (int)off, FIO_SEEK_SET) : 0;
  if (r == (int)off)
    r = write ? fileXioWrite(fd, buf, (int)len) : fileXioRead(fd, buf, (int)len);
  else
    r = r < 0 ? r : -5;
  fileXioClose(fd);
  if (r != (int)len) {
    *rc_out = r;
    return -1;
  }
  return 0;
}

static int read_region(const char *partition, size_t len, int *rc_out) {
  return raw_io(partition, 0, 0, hdr_buf, len, rc_out);
}

/* The KELF at 0x110000, written or compared in whole sectors. */
static int kelf_io(const char *partition, const ppaa_files_t *f, int write, int *rc_out) {
  size_t n = round512(f->kelf_len);
  uint8_t *b = memalign(64, n);
  if (!b) {
    *rc_out = -12;
    return -1;
  }
  int ok;
  if (write) {
    memcpy(b, f->kelf, f->kelf_len);
    memset(b + f->kelf_len, 0, n - f->kelf_len);
    ok = raw_io(partition, 1, PPAA_KELF_OFF, b, n, rc_out) == 0;
  } else {
    ok = raw_io(partition, 0, PPAA_KELF_OFF, b, n, rc_out) == 0 &&
         memcmp(b, f->kelf, f->kelf_len) == 0;
  }
  free(b);
  return ok ? 0 : -1;
}

inst_err_t ppaa_verify_partition_files(const char *partition, const ppaa_files_t *f,
                                       int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  size_t span = ppaa_files_span(f);
  if (!span || read_region(partition, span, rc_out) < 0)
    return ERR_XMB_VERIFY;
  inst_err_t e = ppaa_verify_files(hdr_buf, span, f);
  if (!e && f->kelf && kelf_io(partition, f, 0, rc_out) < 0)
    e = ERR_XMB_VERIFY;
  return e;
}

inst_err_t ppaa_check_partition(const char *partition, const char *syscnf, size_t len,
                                int need_kelf, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  if (read_region(partition, PPAA_REGION_LEN, rc_out) < 0)
    return ERR_XMB_VERIFY;
  if (ppaa_verify(hdr_buf, PPAA_REGION_LEN, syscnf, len) != ERR_OK ||
      !ppaa_has_icons(hdr_buf, PPAA_REGION_LEN) ||
      (need_kelf && !ppaa_has_kelf(hdr_buf, PPAA_REGION_LEN)))
    return ERR_XMB_VERIFY;
  return ERR_OK;
}

int ppaa_partition_iconsys(const char *partition, char *out, size_t outsz) {
  int rc;
  if (read_region(partition, PPAA_ICON_OFF, &rc) < 0)
    return -1;
  return ppaa_read_iconsys(hdr_buf, PPAA_ICON_OFF, out, outsz);
}

inst_err_t ppaa_write_files(const char *partition, const ppaa_files_t *f, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  *rc_out = 0;
  size_t span = ppaa_files_span(f);
  if (!span || read_region(partition, span, rc_out) < 0)
    return ERR_XMB_HEADER_WRITE;
  if (ppaa_apply_files(hdr_buf, span, f) != ERR_OK)
    return ERR_XMB_HEADER_WRITE;
  /* KELF first: the descriptor naming it is written last. */
  if (f->kelf && kelf_io(partition, f, 1, rc_out) < 0)
    return ERR_XMB_HEADER_WRITE;
  if (raw_io(partition, 1, 0, hdr_buf, span, rc_out) < 0)
    return ERR_XMB_HEADER_WRITE;
  /* Byte-for-byte read-back. */
  memset(hdr_buf, 0, span);
  return ppaa_verify_partition_files(partition, f, rc_out);
}
#endif
