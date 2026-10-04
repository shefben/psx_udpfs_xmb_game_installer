#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>

#include "hdd_partitions.h"
#include "util.h"

/* Same argument as libhdd's pfsFormatArg (PFS_ZONE_SIZE, no -f). */
static const int PFS_FORMAT_ARG[1] = {8192};

inst_err_t hdd_status(void) {
  /* libhdd semantics: HDIOC_STATUS 0 = ok, 1/2 = unformatted, >=3 or <0
   * = no drive. */
  int rv = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
  if (rv < 0 || rv >= 3)
    return ERR_HDD_MISSING;
  if (rv != 0)
    return ERR_HDD_NOT_FORMATTED;
  return ERR_OK;
}

int hdd_space_mb(uint32_t *total_mb, uint32_t *free_mb, uint32_t *max_part_mb) {
  uint32_t total =
      (uint32_t)fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0) / 2048;
  int dd = fileXioDopen("hdd0:");
  if (dd < 0)
    return dd;
  uint32_t used = 0;
  iox_dirent_t de;
  while (fileXioDread(dd, &de) > 0)
    if (de.stat.mode != 0)
      used += de.stat.size / 2048;
  fileXioDclose(dd);
  if (total_mb)
    *total_mb = total;
  if (free_mb)
    *free_mb = used > total ? 0 : total - used;
  if (max_part_mb)
    *max_part_mb =
        (uint32_t)fileXioDevctl("hdd0:", HDIOC_MAXSECTOR, NULL, 0, NULL, 0) / 2048;
  return 0;
}

int hdd_list(hdd_part_t *out, int max) {
  int dd = fileXioDopen("hdd0:");
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  int n = 0;
  while (n < max && fileXioDread(dd, &de) > 0) {
    /* Skip free space and sub-partition entries (they repeat the main
     * partition's name with APA_FLAG_SUB set). */
    if (!hdd_dirent_is_main(de.stat.mode, de.stat.attr))
      continue;
    /* APA ids are 32 bytes, not necessarily terminated. */
    memcpy(out[n].name, de.name, APA_NAME_MAX);
    out[n].name[APA_NAME_MAX] = 0;
    out[n].type = (uint16_t)de.stat.mode;
    out[n].size_sectors = de.stat.size;
    n++;
  }
  fileXioDclose(dd);
  return n;
}

int hdd_exists(const char *name) {
  int dd = fileXioDopen("hdd0:");
  if (dd < 0)
    return -1;
  iox_dirent_t de;
  int found = 0;
  while (fileXioDread(dd, &de) > 0) {
    if (de.stat.mode != 0 && strncmp(de.name, name, APA_NAME_MAX) == 0 &&
        strlen(name) <= APA_NAME_MAX) {
      found = 1;
      break;
    }
  }
  fileXioDclose(dd);
  return found;
}

int hdd_stat(const char *name, uint16_t *type, uint32_t *size_sectors,
             uint32_t *start_sector) {
  char path[48];
  iox_stat_t st;
  snprintf(path, sizeof(path), "hdd0:%s", name);
  int r = fileXioGetStat(path, &st);
  if (r < 0)
    return r;
  if (type)
    *type = (uint16_t)st.mode;
  if (size_sectors)
    *size_sectors = st.size;
  if (start_sector)
    *start_sector = st.private_5;
  return 0;
}

inst_err_t hdd_remove_exact(const char *name, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  *rc_out = 0;
  /* The patched ps2hdd-hdl.irx no longer refuses "__" names, so the
   * system-partition protection lives here. */
  if (!partition_remove_allowed(name))
    return ERR_INVALID_ARG;
  int ex = hdd_exists(name);
  if (ex == 0)
    return ERR_OK;
  if (ex < 0)
    return ERR_PARTITION_DELETE;
  char path[48];
  snprintf(path, sizeof(path), "hdd0:%s", name);
  int r = fileXioRemove(path);
  if (r < 0) {
    *rc_out = r;
    return ERR_PARTITION_DELETE;
  }
  return hdd_exists(name) == 0 ? ERR_OK : ERR_PARTITION_DELETE;
}

inst_err_t pfs_create_partition(const char *name, const char *size_str,
                                int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  *rc_out = 0;
  int ex = hdd_exists(name);
  if (ex != 0)
    return ex > 0 ? ERR_PARTITION_EXISTS : ERR_PFS_CREATE;

  char create[64], dev[48];
  snprintf(create, sizeof(create), "hdd0:%s,,,%s,PFS", name, size_str);
  snprintf(dev, sizeof(dev), "hdd0:%s", name);
  int fd = fileXioOpen(create, FIO_O_RDWR | FIO_O_CREAT);
  if (fd < 0) {
    *rc_out = fd;
    return ERR_PFS_CREATE;
  }
  fileXioClose(fd);

  int r = fileXioFormat("pfs:", dev, (const char *)PFS_FORMAT_ARG,
                        sizeof(PFS_FORMAT_ARG));
  if (r < 0) {
    *rc_out = r;
    fileXioRemove(dev);
    return ERR_PFS_FORMAT;
  }
  return ERR_OK;
}

int pfs_mount(const char *mountpoint, const char *partition, int mode) {
  char dev[48];
  snprintf(dev, sizeof(dev), "hdd0:%s", partition);
  fileXioUmount(mountpoint); /* stale mount from an aborted flow */
  return fileXioMount(mountpoint, dev, mode);
}

void pfs_umount(const char *mountpoint) {
  fileXioSync(mountpoint, 0);
  fileXioUmount(mountpoint);
}

int file_write_all(const char *path, const void *data, uint32_t len) {
  int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (fd < 0)
    return fd;
  const uint8_t *p = data;
  uint32_t left = len;
  while (left > 0) {
    int chunk = left > 256 * 1024 ? 256 * 1024 : (int)left;
    int w = fileXioWrite(fd, (void *)p, chunk);
    if (w != chunk) {
      fileXioClose(fd);
      return w < 0 ? w : -5;
    }
    p += chunk;
    left -= (uint32_t)chunk;
  }
  return fileXioClose(fd) < 0 ? -5 : 0;
}

int64_t file_size(const char *path) {
  iox_stat_t st;
  int r = fileXioGetStat(path, &st);
  if (r < 0)
    return r;
  return ((int64_t)st.hisize << 32) | st.size;
}

int file_load(const char *path, void **out, uint32_t max) {
  *out = NULL;
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0)
    return fd;
  int64_t size = fileXioLseek64(fd, 0, FIO_SEEK_END);
  if (size <= 0 || size > (int64_t)max) {
    fileXioClose(fd);
    return size < 0 ? (int)size : -27; /* -EFBIG */
  }
  fileXioLseek64(fd, 0, FIO_SEEK_SET);
  uint8_t *buf = memalign(64, (size_t)size);
  if (!buf) {
    fileXioClose(fd);
    return -12; /* -ENOMEM */
  }
  int64_t got = 0;
  while (got < size) {
    int chunk = (size - got) > 256 * 1024 ? 256 * 1024 : (int)(size - got);
    int r = fileXioRead(fd, buf + got, chunk);
    if (r <= 0) {
      free(buf);
      fileXioClose(fd);
      return r < 0 ? r : -5;
    }
    got += r;
  }
  fileXioClose(fd);
  *out = buf;
  return (int)size;
}
