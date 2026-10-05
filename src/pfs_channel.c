#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "apa_osd_header.h"
#include "hdd_partitions.h"
#include "opl_launcher_payload.h"
#include "pfs_channel.h"
#include "xmb_text.h"

#define W PFS_WORK

static channel_result_t cres(inst_err_t e, int rc, const char *step) {
  channel_result_t r = {e, rc, step};
  return r;
}

static char g_iconsys[1024];

static int osd_files(ppaa_files_t *f, const char *syscnf, const char *title0,
                     const char *title1) {
  const uint8_t *icon;
  uint32_t icon_len;
  size_t n = xmb_render_icon_sys(g_iconsys, sizeof(g_iconsys), title0, title1);
  if (!n)
    return -1;
  payload_osd_icon(&icon, &icon_len);
  *f = (ppaa_files_t){syscnf, strlen(syscnf), g_iconsys, n, icon, icon_len};
  return 0;
}

inst_err_t osd_header_write(const char *partition, const char *syscnf, const char *title0,
                            const char *title1, int *rc_out) {
  ppaa_files_t f;
  if (osd_files(&f, syscnf, title0, title1) < 0)
    return ERR_XMB_HEADER_WRITE;
  return ppaa_write_files(partition, &f, rc_out);
}

inst_err_t osd_header_verify(const char *partition, const char *syscnf, const char *title0,
                             const char *title1, int *rc_out) {
  ppaa_files_t f;
  if (osd_files(&f, syscnf, title0, title1) < 0)
    return ERR_XMB_VERIFY;
  return ppaa_verify_partition_files(partition, &f, rc_out);
}

int install_date(char out[9]) {
  time_t now = time(NULL);
  struct tm *tm = gmtime(&now);
  out[0] = 0;
  return tm ? xmb_date_str(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, out) : -1;
}

static char g_man[2048];

channel_result_t channel_write_res(const char *title, const char *info_sys, uint32_t info_len,
                                   const jacket_pair_t *j) {
  int r = fileXioMkdir(W "res", 0777);
  if (r < 0 && r != -17 /* EEXIST */)
    return cres(ERR_XMB_RESOURCE_WRITE, r, "mkdir res");
  if ((r = file_write_all(W "res/info.sys", info_sys, info_len)) < 0)
    return cres(ERR_XMB_RESOURCE_WRITE, r, "res/info.sys");
  if ((r = file_write_all(W "res/jkt_001.png", j->large, j->large_size)) < 0)
    return cres(ERR_XMB_RESOURCE_WRITE, r, "res/jkt_001.png");
  if ((r = file_write_all(W "res/jkt_002.png", j->small, j->small_size)) < 0)
    return cres(ERR_XMB_RESOURCE_WRITE, r, "res/jkt_002.png");
  /* A manual the channel already has is kept; otherwise the blank one. */
  if (file_size(W "res/man.xml") > 0)
    return cres(ERR_OK, 0, NULL);
  size_t n = xmb_render_man_xml(g_man, sizeof(g_man), title);
  if (!n)
    return cres(ERR_XMB_RESOURCE_WRITE, 0, "res/man.xml");
  const uint8_t *page;
  uint32_t page_len;
  payload_manual_page(&page, &page_len);
  r = fileXioMkdir(W "res/image", 0777);
  if (r < 0 && r != -17)
    return cres(ERR_XMB_RESOURCE_WRITE, r, "mkdir res/image");
  static const char *const PAGES[3] = {W "res/image/0.png", W "res/image/1.png",
                                       W "res/image/2.png"};
  for (int i = 0; i < 3; i++)
    if (file_size(PAGES[i]) <= 0 && (r = file_write_all(PAGES[i], page, page_len)) < 0)
      return cres(ERR_XMB_RESOURCE_WRITE, r, PAGES[i] + strlen(W));
  if ((r = file_write_all(W "res/man.xml", g_man, (uint32_t)n)) < 0)
    return cres(ERR_XMB_RESOURCE_WRITE, r, "res/man.xml");
  return cres(ERR_OK, 0, NULL);
}

channel_result_t channel_populate(const char *partition,
                                  const channel_content_t *c) {
  int r = pfs_mount(W, partition, FIO_MT_RDWR);
  if (r < 0)
    return cres(ERR_PFS_MOUNT, r, "mount");

  channel_result_t out = cres(ERR_OK, 0, NULL);
  if ((r = file_write_all(W "EXECUTE.KELF", c->kelf, c->kelf_size)) < 0)
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "EXECUTE.KELF");
  else
    out = channel_write_res(c->osd_title0, c->info_sys, c->info_sys_len, &c->jkt);
  pfs_umount(W);
  if (out.err)
    return out;

  inst_err_t e = osd_header_write(partition, XMB_SYSTEM_CNF, c->osd_title0, c->osd_title1, &r);
  if (e)
    return cres(e, r, "OSD header (system.cnf, icon.sys, icon)");
  return out;
}

/* Compare a file on the work mount with expected bytes. */
static int file_matches(const char *path, const void *data, uint32_t len) {
  void *buf = NULL;
  int n = file_load(path, &buf, len + 1);
  int ok = n == (int)len && memcmp(buf, data, len) == 0;
  free(buf);
  return ok;
}

channel_result_t channel_verify(const char *partition,
                                const channel_content_t *c) {
  int r = pfs_mount(W, partition, FIO_MT_RDONLY);
  if (r < 0)
    return cres(ERR_PFS_MOUNT, r, "verify mount");
  channel_result_t out = cres(ERR_OK, 0, NULL);
  if (!file_matches(W "EXECUTE.KELF", c->kelf, c->kelf_size))
    out = cres(ERR_XMB_VERIFY, 0, "EXECUTE.KELF");
  else if (!file_matches(W "res/info.sys", c->info_sys, c->info_sys_len))
    out = cres(ERR_XMB_VERIFY, 0, "res/info.sys");
  else if (!file_matches(W "res/jkt_001.png", c->jkt.large, c->jkt.large_size))
    out = cres(ERR_XMB_VERIFY, 0, "res/jkt_001.png");
  else if (!file_matches(W "res/jkt_002.png", c->jkt.small, c->jkt.small_size))
    out = cres(ERR_XMB_VERIFY, 0, "res/jkt_002.png");
  else if (file_size(W "res/man.xml") <= 0 || file_size(W "res/image/0.png") <= 0)
    out = cres(ERR_XMB_VERIFY, 0, "res/man.xml");
  pfs_umount(W);
  if (out.err)
    return out;

  /* The exact system.cnf ("BOOT2 = pfs:/EXECUTE.KELF"), icon.sys and icon. */
  inst_err_t e = osd_header_verify(partition, XMB_SYSTEM_CNF, c->osd_title0, c->osd_title1, &r);
  if (e)
    return cres(e, r, "OSD header (system.cnf, icon.sys, icon)");
  return out;
}
inst_err_t channel_quick_check(const char *partition) {
  if (pfs_mount(W, partition, FIO_MT_RDONLY) < 0)
    return ERR_XMB_VERIFY;
  int ok = file_size(W "EXECUTE.KELF") > 0 && file_size(W "res/info.sys") > 0 &&
           file_size(W "res/jkt_001.png") > 0 && file_size(W "res/jkt_002.png") > 0;
  pfs_umount(W);
  if (!ok)
    return ERR_XMB_VERIFY;
  return ppaa_check_partition(partition, XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF), NULL);
}

#define INFO_SYS_MAX 2048

int channel_get_title(const char *partition, char *out, size_t outsz) {
  if (pfs_mount(W, partition, FIO_MT_RDONLY) < 0)
    return -1;
  void *buf = NULL;
  int n = file_load(W "res/info.sys", &buf, INFO_SYS_MAX);
  pfs_umount(W);
  int r = -1;
  if (n > 0) {
    char *t = buf;
    t[n < INFO_SYS_MAX ? n : INFO_SYS_MAX - 1] = 0;
    r = xmb_info_sys_get(t, "title", out, outsz);
  }
  free(buf);
  return r;
}

channel_result_t channel_set_title(const char *partition, const char *title) {
  int r = pfs_mount(W, partition, FIO_MT_RDWR);
  if (r < 0)
    return cres(ERR_PFS_MOUNT, r, "mount");
  channel_result_t out = cres(ERR_OK, 0, NULL);
  void *buf = NULL;
  static char next[INFO_SYS_MAX];
  int n = file_load(W "res/info.sys", &buf, INFO_SYS_MAX);
  if (n <= 0) {
    out = cres(ERR_XMB_VERIFY, n, "read res/info.sys");
    goto done;
  }
  ((char *)buf)[n < INFO_SYS_MAX ? n : INFO_SYS_MAX - 1] = 0;
  size_t len = xmb_info_sys_retitle(buf, title, next, sizeof(next));
  if (!len) {
    out = cres(ERR_INVALID_ARG, 0, "title");
    goto done;
  }
  if ((r = file_write_all(W "res/info.sys.tmp", next, (uint32_t)len)) < 0 ||
      !file_matches(W "res/info.sys.tmp", next, (uint32_t)len)) {
    fileXioRemove(W "res/info.sys.tmp");
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "res/info.sys.tmp");
    goto done;
  }
  fileXioRemove(W "res/info.sys");
  if ((r = fileXioRename(W "res/info.sys.tmp", W "res/info.sys")) < 0 ||
      !file_matches(W "res/info.sys", next, (uint32_t)len))
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "rename res/info.sys");
done:
  free(buf);
  pfs_umount(W);
  return out;
}

channel_result_t channel_create(const char *partition,
                                const channel_content_t *c) {
  int rc = 0;
  inst_err_t e = pfs_create_partition(partition, CHANNEL_SIZE_STR, &rc);
  if (e)
    return cres(e, rc, "create PFS partition");
  channel_result_t r = channel_populate(partition, c);
  if (!r.err)
    r = channel_verify(partition, c);
  if (r.err)
    hdd_remove_exact(partition, NULL);
  return r;
}
