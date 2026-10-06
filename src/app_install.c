#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_install.h"
#include "crc32.h"
#include "hdd_partitions.h"
#include "opl_launcher_payload.h"
#include "pfs_channel.h"
#include "util.h"
#include "xmb_text.h"

#define W PFS_WORK
#define COPY_CHUNK (256 * 1024)
#define SCAN_DEPTH 4

static int add_file(app_plan_t *p, const char *rel, uint64_t size) {
  if (!app_path_ok(rel) || strlen(rel) >= APP_PATH_MAX || p->nfiles >= APP_FILES_MAX) {
    p->skipped++;
    return -1;
  }
  app_file_t *f = &p->files[p->nfiles++];
  str_copy(f->rel, rel, sizeof(f->rel));
  f->size = size;
  p->bytes += size;
  return p->nfiles - 1;
}

/* Every file under dir/<prefix> (prefix "" or "sub/"), SCAN_DEPTH deep. */
static void scan(app_plan_t *p, const char *dir, const char *prefix, int depth) {
  char path[SOURCE_PATH_MAX];
  if (snprintf(path, sizeof(path), "%s%s%s", dir, prefix[0] ? "/" : "", prefix) >= (int)sizeof(path))
    return;
  size_t pl = strlen(path);
  if (prefix[0] && path[pl - 1] == '/')
    path[pl - 1] = 0;
  int dd = fileXioDopen(path);
  if (dd < 0)
    return;
  static iox_dirent_t de; /* recursion: copied out before descending */
  char subs[16][64];
  int nsub = 0;
  while (fileXioDread(dd, &de) > 0) {
    if (de.name[0] == '.') /* ".", "..", udpfsd's .udpfsd data */
      continue;
    char rel[APP_PATH_MAX + 320]; /* prefix + a 255-byte name; too long: skipped */
    snprintf(rel, sizeof(rel), "%s%s", prefix, de.name);
    if ((de.stat.mode & FIO_S_IFMT) == FIO_S_IFDIR) {
      if (depth < SCAN_DEPTH && nsub < 16 && strlen(de.name) < sizeof(subs[0]) - 1)
        snprintf(subs[nsub++], sizeof(subs[0]), "%s", de.name);
      else
        p->skipped++;
      continue;
    }
    /* the ELF itself is added first, by app_plan_build */
    if (p->nfiles && !strcmp(rel, p->files[p->boot].rel))
      continue;
    add_file(p, rel, ((uint64_t)de.stat.hisize << 32) | de.stat.size);
  }
  fileXioDclose(dd);
  for (int i = 0; i < nsub; i++) {
    char sub[APP_PATH_MAX + 64];
    if (snprintf(sub, sizeof(sub), "%s%s/", prefix, subs[i]) < APP_PATH_MAX)
      scan(p, dir, sub, depth + 1);
    else
      p->skipped++;
  }
}

inst_err_t app_plan_build(const char *elf_path, int with_folder, app_plan_t *p) {
  memset(p, 0, sizeof(*p));
  const char *slash = strrchr(elf_path, '/');
  if (!slash || strlen(elf_path) >= sizeof(p->elf))
    return ERR_INVALID_ARG;
  str_copy(p->elf, elf_path, sizeof(p->elf));
  size_t dl = (size_t)(slash - elf_path);
  memcpy(p->dir, elf_path, dl);
  p->dir[dl] = 0;
  p->with_folder = with_folder;
  /* udpfsd answers getstat only for open files: size by opening */
  int64_t size = file_open_size(elf_path);
  if (size <= 0)
    return ERR_SOURCE_OPEN;
  if ((p->boot = add_file(p, slash + 1, (uint64_t)size)) < 0)
    return ERR_INVALID_ARG; /* a name the partition cannot hold */
  /* title: the folder's name for a folder app, else the ELF's */
  const char *dname = strrchr(p->dir, '/');
  app_title_from_name(with_folder && dname && dname[1] ? dname + 1 : slash + 1, p->title,
                      sizeof(p->title));
  if (with_folder)
    scan(p, p->dir, "", 0);
  return ERR_OK;
}

static app_report_t *fail(app_report_t *rep, inst_err_t e, int rc, const char *step) {
  rep->err = e;
  rep->rc = rc;
  rep->step = step;
  return rep;
}

/* mkdir every parent folder of `rel` on the work mount. */
static void make_parents(const char *rel) {
  char path[8 + APP_PATH_MAX];
  snprintf(path, sizeof(path), W "%s", rel);
  for (char *s = path + strlen(W); (s = strchr(s, '/')) != NULL; s++) {
    *s = 0;
    fileXioMkdir(path, 0777);
    *s = '/';
  }
}

/* Copy one file to the work mount; *crc = CRC-32 of the bytes copied. */
static int copy_file(const char *src, const char *rel, uint64_t size, uint8_t *buf, uint32_t *crc) {
  char dst[8 + APP_PATH_MAX];
  snprintf(dst, sizeof(dst), W "%s", rel);
  make_parents(rel);
  int in = fileXioOpen(src, FIO_O_RDONLY);
  if (in < 0)
    return in;
  int out = fileXioOpen(dst, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (out < 0) {
    fileXioClose(in);
    return out;
  }
  uint64_t done = 0;
  int r = 0;
  *crc = 0;
  while (done < size) {
    int want = size - done > COPY_CHUNK ? COPY_CHUNK : (int)(size - done);
    int n = fileXioRead(in, buf, want);
    if (n <= 0) {
      r = n < 0 ? n : -5;
      break;
    }
    if (fileXioWrite(out, buf, n) != n) {
      r = -5;
      break;
    }
    *crc = crc32_update(*crc, buf, (size_t)n);
    done += (uint64_t)n;
  }
  fileXioClose(in);
  if (fileXioClose(out) < 0 && !r)
    r = -5;
  return r;
}

/* CRC-32 of a file on the work mount; -1 if it is not `size` long. */
static int crc_file(const char *rel, uint64_t size, uint8_t *buf, uint32_t *crc) {
  char path[8 + APP_PATH_MAX];
  snprintf(path, sizeof(path), W "%s", rel);
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0)
    return fd;
  uint64_t done = 0;
  int n;
  *crc = 0;
  while ((n = fileXioRead(fd, buf, COPY_CHUNK)) > 0) {
    *crc = crc32_update(*crc, buf, (size_t)n);
    done += (uint64_t)n;
  }
  fileXioClose(fd);
  return n < 0 ? n : done == size ? 0 : -1;
}

static app_report_t *copy_all(const app_plan_t *p, app_progress_fn progress, uint8_t *buf,
                              uint32_t *crcs, app_report_t *rep) {
  for (int i = 0; i < p->nfiles; i++) {
    const app_file_t *f = &p->files[i];
    char src[SOURCE_PATH_MAX + APP_PATH_MAX];
    snprintf(src, sizeof(src), "%s/%s", p->dir, f->rel);
    if (progress)
      progress("Copying", i + 1, p->nfiles, f->rel);
    int r = copy_file(src, f->rel, f->size, buf, &crcs[i]);
    if (r < 0)
      return fail(rep, ERR_SOURCE_READ, r, f->rel);
  }
  app_cfg_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  str_copy(cfg.boot, p->files[p->boot].rel, sizeof(cfg.boot));
  char text[1024];
  size_t n = app_cfg_render(&cfg, text, sizeof(text));
  int r = n ? file_write_all(W APP_CFG_FILE, text, (uint32_t)n) : -1;
  if (r < 0)
    return fail(rep, ERR_XMB_RESOURCE_WRITE, r, APP_CFG_FILE);
  return rep;
}

static app_report_t *verify_all(const app_plan_t *p, app_progress_fn progress, uint8_t *buf,
                                const uint32_t *crcs, app_report_t *rep) {
  for (int i = 0; i < p->nfiles; i++) {
    uint32_t crc;
    if (progress)
      progress("Verifying", i + 1, p->nfiles, p->files[i].rel);
    int r = crc_file(p->files[i].rel, p->files[i].size, buf, &crc);
    if (r < 0 || crc != crcs[i])
      return fail(rep, ERR_XMB_VERIFY, r, p->files[i].rel);
  }
  void *cfgbuf = NULL;
  int n = file_load(W APP_CFG_FILE, &cfgbuf, 4096);
  app_cfg_t cfg;
  int ok = n > 0 && app_cfg_parse(cfgbuf, (size_t)n, &cfg) == 0 &&
           !strcmp(cfg.boot, p->files[p->boot].rel);
  free(cfgbuf);
  if (!ok)
    return fail(rep, ERR_XMB_VERIFY, n, APP_CFG_FILE);
  return rep;
}

void app_install(const app_plan_t *p, app_progress_fn progress, app_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  payload_t kelf;
  if ((rep->err = payload_app_launcher(&kelf))) {
    rep->step = "this build carries no app launcher (dev build?)";
    return;
  }
  uint32_t size_mb = 0;
  const char *size_str = app_size_str((p->bytes + (1u << 20) - 1) >> 20, &size_mb);
  if (!size_str) {
    fail(rep, ERR_NO_SPACE, 0, "app bigger than 2 GiB");
    return;
  }

  /* next free PP.APPS-NNNNN */
  const space_part_t *parts;
  int np = hdd_space_list(&parts);
  if (np < 0) {
    fail(rep, ERR_HDD_MISSING, np, "partition list");
    return;
  }
  const char *names[SPACE_MAX_PARTS];
  for (int i = 0; i < np; i++)
    names[i] = parts[i].name;
  if (app_partition_name(p->title, names, np, rep->partition) < 0) {
    fail(rep, ERR_PARTITION_EXISTS, 0, "no free app number");
    return;
  }
  const char *app_id = rep->partition + 3; /* "APPS-NNNNN.." */
  char id[11];
  memcpy(id, app_id, 10);
  id[10] = 0;

  /* channel: launcher, res/, header "BOOT2 = pfs:/EXECUTE.KELF" */
  char today[9], info[1024];
  install_date(today);
  channel_content_t c;
  memset(&c, 0, sizeof(c));
  c.kelf = kelf.data;
  c.kelf_size = kelf.size;
  c.info_sys = info;
  c.info_sys_len = (uint32_t)xmb_render_info_sys(info, sizeof(info), p->title, id, today);
  payload_app_jackets(&c.jkt);
  c.osd_title0 = p->title;
  c.osd_title1 = id;

  int rc = 0;
  if ((rep->err = pfs_create_partition(rep->partition, size_str, &rc))) {
    rep->rc = rc;
    rep->step = "create partition";
    payload_release(&kelf);
    return;
  }
  uint8_t *buf = memalign(64, COPY_CHUNK);
  uint32_t *crcs = calloc((size_t)p->nfiles, sizeof(uint32_t));
  channel_result_t cr = {0};
  if (!buf || !crcs) {
    fail(rep, ERR_INTERNAL, 0, "out of memory");
  } else if ((cr = channel_populate(rep->partition, &c)).err) {
    fail(rep, cr.err, cr.rc, cr.step);
  } else if ((rc = pfs_mount(W, rep->partition, FIO_MT_RDWR)) < 0) {
    fail(rep, ERR_PFS_MOUNT, rc, "mount");
  } else {
    copy_all(p, progress, buf, crcs, rep);
    pfs_umount(W);
    if (!rep->err && (rc = pfs_mount(W, rep->partition, FIO_MT_RDONLY)) < 0)
      fail(rep, ERR_PFS_MOUNT, rc, "verify mount");
    else if (!rep->err) {
      verify_all(p, progress, buf, crcs, rep);
      pfs_umount(W);
    }
    if (!rep->err && (cr = channel_verify(rep->partition, &c)).err)
      fail(rep, cr.err, cr.rc, cr.step);
  }
  free(buf);
  free(crcs);
  payload_release(&kelf);
  if (rep->err)
    hdd_remove_exact(rep->partition, NULL); /* no broken channel left */
}
