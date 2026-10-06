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

/* The whole APA list (main partitions, sub-partitions, free space) with
 * start sectors (the driver is an APA_OSD_VER build: private_5 = start). */
static char g_space_names[SPACE_MAX_PARTS][APA_NAME_MAX + 1];
static space_part_t g_space_parts[SPACE_MAX_PARTS];

int hdd_space_list(const space_part_t **out) {
  int dd = fileXioDopen("hdd0:");
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  int n = 0;
  while (n < SPACE_MAX_PARTS && fileXioDread(dd, &de) > 0) {
    memcpy(g_space_names[n], de.name, APA_NAME_MAX);
    g_space_names[n][APA_NAME_MAX] = 0;
    g_space_parts[n] = (space_part_t){g_space_names[n], de.stat.mode, de.stat.attr,
                                      de.stat.private_5, de.stat.size};
    n++;
  }
  fileXioDclose(dd);
  *out = g_space_parts;
  return n;
}

int hdd_usage(space_usage_t *u, uint32_t *hdd_free_mb) {
  const space_part_t *p;
  int n = hdd_space_list(&p);
  if (n < 0)
    return n;
  space_tally(p, n, u);
  uint32_t total = 0, free_mb = 0;
  int r = hdd_space_raw(&total, &free_mb, NULL);
  if (hdd_free_mb)
    *hdd_free_mb = r < 0 ? 0 : free_mb;
  return r < 0 ? r : 0;
}

inst_err_t hdd_space_check(uint32_t add_mb) {
  space_usage_t u;
  uint32_t free_mb = 0;
  if (hdd_usage(&u, &free_mb) < 0)
    return ERR_HDD_MISSING;
  return space_check(&u, add_mb, free_mb);
}

inst_err_t hdd_space_guard_new(const char *name, int *rc_out) {
  const space_part_t *p;
  int n = hdd_space_list(&p);
  if (n < 0 || !space_name_beyond(p, n, name))
    return ERR_OK;
  /* The driver placed it past 128 GiB: it must not stay. */
  hdd_remove_exact(name, rc_out);
  return ERR_DATA_LIMIT;
}

int hdd_space_mb(uint32_t *total_mb, uint32_t *free_mb, uint32_t *max_part_mb) {
  uint32_t total = 0, hdd_free = 0;
  int r = hdd_space_raw(&total, &hdd_free, max_part_mb);
  if (r < 0)
    return r;
  if (total_mb)
    *total_mb = total;
  if (free_mb) {
    /* Never more than the 128 GiB limit for games and data leaves. */
    space_usage_t u;
    const space_part_t *p;
    int n = hdd_space_list(&p);
    if (n < 0)
      return n;
    space_tally(p, n, &u);
    *free_mb = (uint32_t)space_usable_mb(&u, hdd_free);
  }
  return 0;
}

/* Raw drive figures: total, free (total - all partitions), max bucket. */
int hdd_space_raw(uint32_t *total_mb, uint32_t *free_mb, uint32_t *max_part_mb) {
  /* Driver errors are negative; never let one turn into a huge size
   * (the planner would then assume the 4 GiB maximum). */
  int ts = fileXioDevctl("hdd0:", HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
  int ms = fileXioDevctl("hdd0:", HDIOC_MAXSECTOR, NULL, 0, NULL, 0);
  if (ts <= 0)
    return ts < 0 ? ts : -5;
  if (ms <= 0)
    return ms < 0 ? ms : -5;
  uint32_t total = (uint32_t)ts / 2048;
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
    *max_part_mb = (uint32_t)ms / 2048;
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

int installer_partition_migrate(void) {
  switch (installer_name_action(hdd_exists(INSTALLER_PARTITION),
                                hdd_exists(INSTALLER_LEGACY_NAME))) {
  case INSTALLER_NAME_RENAME_LEGACY: {
    /* APA rename (HDLGameInstaller apa-hdl hddReName): only the header
     * id changes; the PFS contents (config/, state/ journals) stay. */
    int r = fileXioRename("hdd0:" INSTALLER_LEGACY_NAME, "hdd0:" INSTALLER_PARTITION);
    if (r < 0)
      return r;
    return hdd_exists(INSTALLER_PARTITION) > 0 && hdd_exists(INSTALLER_LEGACY_NAME) == 0
               ? 0
               : -5;
  }
  default:
    return 0;
  }
}

inst_err_t hdd_rename_game(const char *from, const char *to, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  *rc_out = 0;
  if (!(partition_pair_matches(from, to) || partition_pair_matches(to, from)))
    return ERR_INVALID_ARG;
  uint16_t type = 0;
  int r = hdd_stat(from, &type, NULL, NULL);
  if (r < 0) {
    *rc_out = r;
    return ERR_PARTITION_RENAME;
  }
  if (type != APA_TYPE_HDL_ID) {
    *rc_out = type;
    return ERR_INVALID_ARG;
  }
  if (hdd_exists(to) != 0)
    return ERR_PARTITION_EXISTS;
  char a[48], b[48];
  snprintf(a, sizeof(a), "hdd0:%s", from);
  snprintf(b, sizeof(b), "hdd0:%s", to);
  r = fileXioRename(a, b);
  if (r < 0) {
    *rc_out = r;
    return ERR_PARTITION_RENAME;
  }
  return hdd_exists(to) > 0 && hdd_exists(from) == 0 ? ERR_OK : ERR_PARTITION_RENAME;
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
  /* Shape check before touching the device (also rejects ','). */
  if (!partition_remove_allowed(name, APA_TYPE_PFS_ID) &&
      !partition_remove_allowed(name, APA_TYPE_HDL_ID))
    return ERR_INVALID_ARG;
  int ex = hdd_exists(name);
  if (ex == 0)
    return ERR_OK;
  if (ex < 0)
    return ERR_PARTITION_DELETE;
  /* Whitelist by name AND actual APA type: a hidden game must be HDL,
   * a channel or installer partition must be PFS. The driver enforces
   * its own, looser rule (tools/driver/remove_policy.h). */
  uint16_t type = 0;
  int r0 = hdd_stat(name, &type, NULL, NULL);
  if (r0 < 0) {
    *rc_out = r0;
    return ERR_PARTITION_DELETE;
  }
  if (!partition_remove_allowed(name, type)) {
    *rc_out = type;
    return ERR_INVALID_ARG;
  }
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

  /* 128 GiB limit for games and data, before anything is created. */
  uint32_t want_mb = 0;
  for (const char *s = size_str; *s >= '0' && *s <= '9'; s++)
    want_mb = want_mb * 10 + (uint32_t)(*s - '0');
  if (strchr(size_str, 'G'))
    want_mb *= 1024;
  inst_err_t lim = hdd_space_check(want_mb ? want_mb : 1);
  if (lim)
    return lim;
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
    hdd_remove_exact(name, NULL); /* typed whitelist applies here too */
    return ERR_PFS_FORMAT;
  }
  return hdd_space_guard_new(name, rc_out);
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

int64_t file_open_size(const char *path) {
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0)
    return fd;
  int64_t size = fileXioLseek64(fd, 0, FIO_SEEK_END);
  fileXioClose(fd);
  return size;
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
