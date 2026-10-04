#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "apa_osd_header.h"
#include "hdd_partitions.h"
#include "pfs_channel.h"
#include "xmb_text.h"

#define W PFS_WORK

static channel_result_t cres(inst_err_t e, int rc, const char *step) {
  channel_result_t r = {e, rc, step};
  return r;
}

channel_result_t channel_populate(const char *partition,
                                  const channel_content_t *c) {
  int r = pfs_mount(W, partition, FIO_MT_RDWR);
  if (r < 0)
    return cres(ERR_PFS_MOUNT, r, "mount");

  channel_result_t out = cres(ERR_OK, 0, NULL);
  r = fileXioMkdir(W "res", 0777);
  if (r < 0 && r != -17 /* EEXIST */) {
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "mkdir res");
    goto done;
  }
  if ((r = file_write_all(W "EXECUTE.KELF", c->kelf, c->kelf_size)) < 0) {
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "EXECUTE.KELF");
    goto done;
  }
  if ((r = file_write_all(W "res/info.sys", c->info_sys, c->info_sys_len)) < 0) {
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "res/info.sys");
    goto done;
  }
  if ((r = file_write_all(W "res/jkt_001.png", c->jacket, c->jacket_size)) < 0) {
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "res/jkt_001.png");
    goto done;
  }
  if ((r = file_write_all(W "res/jkt_002.png", c->jacket, c->jacket_size)) < 0) {
    out = cres(ERR_XMB_RESOURCE_WRITE, r, "res/jkt_002.png");
    goto done;
  }
done:
  pfs_umount(W);
  if (out.err)
    return out;

  inst_err_t e = ppaa_write_partition(partition, XMB_SYSTEM_CNF,
                                      strlen(XMB_SYSTEM_CNF), &r);
  if (e)
    return cres(e, r, "PPAA/system.cnf header");
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
  else if (!file_matches(W "res/jkt_001.png", c->jacket, c->jacket_size))
    out = cres(ERR_XMB_VERIFY, 0, "res/jkt_001.png");
  else if (!file_matches(W "res/jkt_002.png", c->jacket, c->jacket_size))
    out = cres(ERR_XMB_VERIFY, 0, "res/jkt_002.png");
  pfs_umount(W);
  if (out.err)
    return out;

  /* ppaa_verify compares the exact system.cnf, which contains
   * "BOOT2 = pfs:/EXECUTE.KELF". */
  inst_err_t e = ppaa_verify_partition(partition, XMB_SYSTEM_CNF,
                                       strlen(XMB_SYSTEM_CNF), &r);
  if (e)
    return cres(e, r, "PPAA/system.cnf header");
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
  return ppaa_verify_partition(partition, XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF),
                               NULL);
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
