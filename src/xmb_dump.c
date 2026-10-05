#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "browser.h"
#include "hdd_partitions.h"
#include "partname.h"
#include "xmb_dump.h"

/* Read-only: partitions are opened O_RDONLY and mounted FIO_MT_RDONLY;
 * everything is written to the USB drive. */

#define DUMP_DIR USB_ROOT "xmb-dump"
#define HDR_BYTES (128 * 1024)   /* partition +0x1000: OSD header and icons */
#define FILE_FULL (1024 * 1024)  /* smaller files are copied whole */
#define FILE_HEAD 4096           /* larger ones (KELF, ELF, VCD): first 4 KiB */
#define MAX_PARTS 128

static uint8_t buf[HDR_BYTES] __attribute__((aligned(64)));
static char listing[16384];
static int loff;

static void lst(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void lst(const char *fmt, ...) {
  if (loff >= (int)sizeof(listing) - 1)
    return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(listing + loff, sizeof(listing) - loff, fmt, ap);
  va_end(ap);
  if (n > 0)
    loff += n < (int)sizeof(listing) - loff ? n : (int)sizeof(listing) - 1 - loff;
}

/* Partition names hold '.' and printable ASCII; keep them as folder names. */
static void safe_name(const char *in, char *out, size_t sz) {
  size_t n = 0;
  for (; *in && n < sz - 1; in++)
    out[n++] = (*in >= 0x21 && *in < 0x7F && !strchr("\\/:*?\"<>|", *in)) ? *in : '_';
  out[n] = 0;
}

static int copy_file(const char *src, const char *dst, int size) {
  int len = size > FILE_FULL ? FILE_HEAD : size;
  int in = fileXioOpen(src, FIO_O_RDONLY);
  if (in < 0)
    return in;
  void *b = malloc(len > 0 ? len : 1);
  int r = b ? fileXioRead(in, b, len) : -12;
  fileXioClose(in);
  if (r == len)
    r = file_write_all(dst, b, (uint32_t)len);
  free(b);
  return r;
}

/* Walk `dir` on the mounted channel; mirror small files under `out`. */
static int walk(const char *dir, const char *out, int depth) {
  int dd = fileXioDopen(dir);
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  int n = 0;
  while (fileXioDread(dd, &de) > 0) {
    if (!strcmp(de.name, ".") || !strcmp(de.name, ".."))
      continue;
    static char src[640], dst[640]; /* not kept across the recursive call */
    snprintf(src, sizeof(src), "%s%s", dir, de.name);
    snprintf(dst, sizeof(dst), "%s/%s", out, de.name);
    if (FIO_S_ISDIR(de.stat.mode)) {
      lst("  %s/  mode %o\n", src + strlen(PFS_WORK), (unsigned)de.stat.mode);
      if (depth < 4) {
        char sub[648], dsub[640];
        snprintf(sub, sizeof(sub), "%s/", src);
        memcpy(dsub, dst, sizeof(dsub));
        fileXioMkdir(dsub, 0777);
        n += walk(sub, dsub, depth + 1);
      }
    } else {
      lst("  %s  %u bytes  mode %o\n", src + strlen(PFS_WORK), (unsigned)de.stat.size,
          (unsigned)de.stat.mode);
      copy_file(src, dst, (int)de.stat.size);
      n++;
    }
  }
  fileXioDclose(dd);
  return n;
}

static void dump_one(const hdd_part_t *p) {
  char folder[48], path[160];
  safe_name(p->name, folder, sizeof(folder));
  char out[96];
  snprintf(out, sizeof(out), DUMP_DIR "/%s", folder);
  fileXioMkdir(out, 0777);

  uint32_t start = 0, size = 0;
  uint16_t type = 0;
  hdd_stat(p->name, &type, &size, &start);
  lst("%s\n  APA type 0x%04x  size %lu sectors  start %lu\n", p->name, type,
      (unsigned long)size, (unsigned long)start);
  /* OSD header area: hdd0:<name> offset 0 = partition + 0x1000. */
  char dev[48];
  snprintf(dev, sizeof(dev), "hdd0:%s", p->name);
  int fd = fileXioOpen(dev, FIO_O_RDONLY);
  if (fd >= 0) {
    int r = fileXioRead(fd, buf, HDR_BYTES);
    fileXioClose(fd);
    if (r > 0) {
      snprintf(path, sizeof(path), "%s/osd_header_0x1000.bin", out);
      file_write_all(path, buf, (uint32_t)r);
    }
  }

  if (type == APA_TYPE_PFS_ID && pfs_mount(PFS_WORK, p->name, FIO_MT_RDONLY) == 0) {
    char files[112];
    snprintf(files, sizeof(files), "%s/files", out);
    fileXioMkdir(files, 0777);
    walk(PFS_WORK, files, 0);
    pfs_umount(PFS_WORK);
  }
  lst("\n");
}

int xmb_dump_to_usb(int *parts_out) {
  static hdd_part_t parts[MAX_PARTS];
  *parts_out = 0;
  int n = hdd_list(parts, MAX_PARTS);
  if (n < 0)
    return n;
  int r = fileXioMkdir(DUMP_DIR, 0777);
  if (r < 0 && r != -17 /* EEXIST */)
    return r;
  loff = 0;
  listing[0] = 0;
  /* All partitions with their type and size, then details of every
   * channel-like one (PP.*, +OPL is skipped: only PP. affect the XMB). */
  for (int i = 0; i < n; i++)
    lst("0x%04x %8lu MiB  %s\n", parts[i].type, (unsigned long)(parts[i].size_sectors / 2048),
        parts[i].name);
  lst("\n");
  for (int i = 0; i < n; i++) {
    if (strncmp(parts[i].name, "PP.", 3))
      continue;
    dump_one(&parts[i]);
    (*parts_out)++;
  }
  return file_write_all(DUMP_DIR "/partitions.txt", listing, (uint32_t)loff);
}
