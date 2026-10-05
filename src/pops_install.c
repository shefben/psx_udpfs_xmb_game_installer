#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "apa_osd_header.h"
#include "app_state.h"
#include "crc32.h"
#include "hdd_partitions.h"
#include "pops_install.h"
#include "source_udpfs.h"
#include "util.h"
#include "xmb_text.h"

#define W PFS_WORK
#define POPS_BUF (512 * 1024)
#define POPS_KELF_MAX (2 * 1024 * 1024)
#define POPS_FILE_MAX (8 * 1024 * 1024)

static uint8_t buf[POPS_BUF] __attribute__((aligned(64)));
static GameSource g_src;
static udpfs_src_t g_u;

inst_err_t pops_plan_set_title(pops_plan_t *p, const char *title) {
  char clean[64], hidden[APA_NAME_MAX + 1];
  xmb_sanitize_value(title, clean, sizeof(clean));
  if (!clean[0])
    str_copy(clean, p->vcd.part_id, sizeof(clean));
  str_copy(p->title, clean, sizeof(p->title));
  /* Same PP.XXXX-NNNNN..TITLE shape as PS2 channels (the DESR XMB lists
   * only those); a PS1 game has no __. partner. */
  if (build_game_partition_pair(p->vcd.boot_id, p->title, p->partition, hidden))
    return ERR_INVALID_ARG;
  return ERR_OK;
}

inst_err_t pops_plan_build(const char *path, pops_plan_t *p, int *rc_out) {
  memset(p, 0, sizeof(*p));
  *rc_out = 0;
  str_copy(p->source_path, path, sizeof(p->source_path));
  source_udpfs_init(&g_src, &g_u);
  inst_err_t e = source_open(&g_src, path);
  if (!e)
    e = vcd_probe(&g_src, &p->vcd);
  *rc_out = g_src.last_rc;
  source_close(&g_src);
  if (e)
    return e;
  /* Title from the file name ("Metal Gear Solid.VCD"). */
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  char t[64];
  str_copy(t, base, sizeof(t));
  size_t n = strlen(t);
  if (n > 4 && str_ends_with_ci(t, ".vcd"))
    t[n - 4] = 0;
  if (pops_plan_set_title(p, t))
    return ERR_SOURCE_SYSTEM_CNF;
  p->size_mb = pops_partition_mb(p->vcd.bytes, p->size_str, sizeof(p->size_str));
  return p->size_mb < 0 ? ERR_HDL_PLAN : ERR_OK;
}

/* <folder of the VCD>/<name>, else <device>/POPS/<name>. */
static int find_pops_file(const char *vcd_path, const char *name, char *out, size_t sz) {
  const char *slash = strrchr(vcd_path, '/');
  if (slash) {
    snprintf(out, sz, "%.*s/%s", (int)(slash - vcd_path), vcd_path, name);
    if (file_size(out) > 0)
      return 0;
  }
  const char *colon = strchr(vcd_path, ':');
  if (colon) {
    snprintf(out, sz, "%.*s:/POPS/%s", (int)(colon - vcd_path), vcd_path, name);
    if (file_size(out) > 0)
      return 0;
  }
  return -1;
}

/* POPS.ELF + IOPRP252.IMG in __common/POPS (only when missing) and the
 * game's VMC folder. */
static inst_err_t install_runtime(const pops_plan_t *p, install_report_t *rep) {
  static const char *const files[] = {POPS_ELF, POPS_IOPRP};
  int r = pfs_mount(W, "__common", FIO_MT_RDWR);
  if (r < 0) {
    rep->rc = r;
    rep->detail = "mount __common";
    return ERR_PFS_MOUNT;
  }
  inst_err_t e = ERR_OK;
  fileXioMkdir(W "POPS", 0777);
  for (int i = 0; i < 2 && !e; i++) {
    char dst[64], src[SOURCE_PATH_MAX + 16];
    snprintf(dst, sizeof(dst), W "POPS/%s", files[i]);
    if (file_size(dst) > 0)
      continue;
    if (find_pops_file(p->source_path, files[i], src, sizeof(src)) < 0) {
      rep->detail = i == 0 ? "POPS.ELF not found next to the VCD or in POPS/"
                           : "IOPRP252.IMG not found next to the VCD or in POPS/";
      e = ERR_KELF_MISSING;
      break;
    }
    void *data = NULL;
    int n = file_load(src, &data, POPS_FILE_MAX);
    if (n <= 0 || (r = file_write_all(dst, data, (uint32_t)n)) < 0 || file_size(dst) != n) {
      rep->rc = n <= 0 ? n : r;
      rep->detail = files[i];
      e = ERR_XMB_RESOURCE_WRITE;
    }
    free(data);
  }
  if (!e) {
    char vmc[APA_NAME_MAX + 1], dir[64];
    pops_vmc_dir(p->partition, vmc, sizeof(vmc));
    snprintf(dir, sizeof(dir), W "POPS/%s", vmc);
    fileXioMkdir(dir, 0777); /* POPStarter keeps SLOT0/1.VMC there */
  }
  pfs_umount(W);
  return e;
}

static void progress(const install_ui_t *ui, time_t start, time_t *last, uint64_t done,
                     uint64_t total, int *abort) {
  time_t now = time(NULL);
  if (!ui || (now == *last && done != total))
    return;
  *last = now;
  if (ui->progress)
    ui->progress(ui->ctx, done, total, (uint32_t)(now - start));
  if (ui->should_abort && ui->should_abort(ui->ctx))
    *abort = 1;
}

/* Source -> W IMAGE0.VCD, CRC-32 of every byte. */
static inst_err_t copy_vcd(const pops_plan_t *p, const install_ui_t *ui, install_report_t *rep) {
  int fd = fileXioOpen(W POPS_IMAGE, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (fd < 0) {
    rep->rc = fd;
    return ERR_XMB_RESOURCE_WRITE;
  }
  inst_err_t e = ERR_OK;
  uint64_t total = p->vcd.bytes, done = 0;
  uint32_t crc = 0;
  time_t start = time(NULL), last = 0;
  int abort = 0;
  if (g_src.ops->seek(&g_src, 0, SRC_SEEK_SET) != 0)
    e = ERR_SOURCE_READ;
  while (!e && done < total) {
    uint32_t want = total - done > POPS_BUF ? POPS_BUF : (uint32_t)(total - done);
    if ((e = source_read_exact(&g_src, buf, want))) {
      rep->rc = g_src.last_rc;
      break;
    }
    crc = crc32_update(crc, buf, want);
    int w = fileXioWrite(fd, buf, (int)want);
    if (w != (int)want) {
      rep->rc = w;
      e = ERR_XMB_RESOURCE_WRITE;
      break;
    }
    done += want;
    progress(ui, start, &last, done, total, &abort);
    if (abort)
      e = ERR_USER_ABORT;
  }
  if (fileXioClose(fd) < 0 && !e)
    e = ERR_XMB_RESOURCE_WRITE;
  rep->bytes_written = done;
  rep->source_crc32 = crc;
  return e;
}

/* Read W IMAGE0.VCD back; ERR_USER_ABORT with *skipped = 1 when the user
 * skipped (START). */
static inst_err_t verify_vcd(const pops_plan_t *p, const install_ui_t *ui,
                             install_report_t *rep) {
  int fd = fileXioOpen(W POPS_IMAGE, FIO_O_RDONLY);
  if (fd < 0) {
    rep->rc = fd;
    return ERR_XMB_VERIFY;
  }
  inst_err_t e = ERR_OK;
  uint64_t total = p->vcd.bytes, done = 0;
  uint32_t crc = 0;
  time_t start = time(NULL), last = 0;
  int abort = 0;
  while (done < total) {
    uint32_t want = total - done > POPS_BUF ? POPS_BUF : (uint32_t)(total - done);
    int r = fileXioRead(fd, buf, (int)want);
    if (r != (int)want) {
      rep->rc = r;
      e = ERR_XMB_VERIFY;
      break;
    }
    crc = crc32_update(crc, buf, want);
    done += want;
    progress(ui, start, &last, done, total, &abort);
    if (abort) {
      e = ERR_USER_ABORT;
      break;
    }
  }
  fileXioClose(fd);
  rep->bytes_verified = done;
  if (!e) {
    rep->have_crc = 1;
    rep->installed_crc32 = crc;
    if (crc != rep->source_crc32) {
      rep->detail = "CRC-32 of IMAGE0.VCD on the HDD != source";
      e = ERR_XMB_VERIFY;
    }
  }
  return e;
}

static void stage(const install_ui_t *ui, install_report_t *rep, install_stage_t s) {
  rep->stage = s;
  if (ui && ui->stage)
    ui->stage(ui->ctx, s);
}

void pops_install(pops_plan_t *p, const install_ui_t *ui, install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  void *kelf = NULL, *jkt_owned = NULL;
  int created = 0;
  stage(ui, rep, STAGE_PREPARING);

  if (game_source_is_server(p->source_path) && g_app.net != NETWORK_READY) {
    rep->err = ERR_NETWORK;
    goto out;
  }
  source_udpfs_init(&g_src, &g_u);
  if ((rep->err = source_open(&g_src, p->source_path))) {
    rep->rc = g_src.last_rc;
    goto out;
  }
  vcd_info_t again;
  if ((rep->err = vcd_probe(&g_src, &again)))
    goto out;
  if (strcmp(again.boot_id, p->vcd.boot_id) || again.bytes != p->vcd.bytes) {
    rep->err = ERR_SOURCE_INVALID_ISO;
    rep->detail = "source changed since it was selected";
    goto out;
  }
  if (hdd_exists(p->partition) != 0) {
    rep->err = ERR_PARTITION_EXISTS;
    goto out;
  }
  uint32_t free_mb = 0;
  if (hdd_space_mb(NULL, &free_mb, NULL) < 0 || (uint32_t)p->size_mb > free_mb) {
    rep->err = ERR_NO_SPACE;
    goto out;
  }
  /* Everything that can be missing is checked before the partition. */
  char kpath[SOURCE_PATH_MAX + 16];
  int klen = -1;
  if (find_pops_file(p->source_path, POPS_KELF, kpath, sizeof(kpath)) == 0)
    klen = file_load(kpath, &kelf, POPS_KELF_MAX);
  if (klen <= 0) {
    rep->err = ERR_KELF_MISSING;
    rep->detail = "POPSTARTER.KELF not found next to the VCD or in POPS/";
    goto out;
  }
  if ((rep->err = install_runtime(p, rep)))
    goto out;

  stage(ui, rep, STAGE_CREATING_HDL);
  if ((rep->err = pfs_create_partition(p->partition, p->size_str, &rep->rc)))
    goto out;
  created = 1;
  int r = pfs_mount(W, p->partition, FIO_MT_RDWR);
  if (r < 0) {
    rep->err = ERR_PFS_MOUNT;
    rep->rc = r;
    goto out;
  }
  fileXioMkdir(W "res", 0777);
  if ((r = file_write_all(W "EXECUTE.KELF", kelf, (uint32_t)klen)) < 0) {
    rep->err = ERR_XMB_RESOURCE_WRITE;
    rep->rc = r;
    rep->detail = "EXECUTE.KELF";
    goto out;
  }
  char info[1024];
  xmb_game_info_t gi;
  int have_gi = game_load_info(p->vcd.boot_id, &gi);
  uint32_t info_len = (uint32_t)xmb_game_info_sys_ex(info, sizeof(info), p->title, p->vcd.boot_id,
                                                     have_gi ? &gi : NULL);
  const uint8_t *jkt;
  uint32_t jkt_size;
  rep->jacket = game_load_jacket(p->vcd.boot_id, p->source_path, &jkt, &jkt_size, &jkt_owned);
  if (!info_len || file_write_all(W "res/info.sys", info, info_len) < 0 ||
      file_write_all(W "res/jkt_001.png", jkt, jkt_size) < 0 ||
      file_write_all(W "res/jkt_002.png", jkt, jkt_size) < 0) {
    rep->err = ERR_XMB_RESOURCE_WRITE;
    rep->detail = "res/";
    goto out;
  }
  stage(ui, rep, STAGE_COPYING);
  if ((rep->err = copy_vcd(p, ui, rep)))
    goto out;
  source_close(&g_src);
  pfs_umount(W);

  stage(ui, rep, STAGE_VALIDATING);
  if ((r = pfs_mount(W, p->partition, FIO_MT_RDONLY)) < 0) {
    rep->err = ERR_PFS_MOUNT;
    rep->rc = r;
    goto out;
  }
  if (file_size(W "EXECUTE.KELF") != klen || file_size(W "res/info.sys") != info_len) {
    rep->err = ERR_XMB_VERIFY;
    rep->detail = "EXECUTE.KELF / res/info.sys";
    goto out;
  }
  rep->err = verify_vcd(p, ui, rep);
  if (rep->err == ERR_USER_ABORT && ui && ui->skip_verify && ui->skip_verify(ui->ctx)) {
    rep->verify_skipped = 1; /* copied in full with its CRC; read-back skipped */
    rep->err = ERR_OK;
  }
  if (rep->err)
    goto out;
  pfs_umount(W);

  /* Last: the header that makes the channel appear in the XMB. */
  stage(ui, rep, STAGE_CREATING_CHANNEL);
  if ((rep->err = ppaa_write_partition(p->partition, XMB_SYSTEM_CNF, strlen(XMB_SYSTEM_CNF),
                                       &rep->rc))) {
    rep->detail = "PPAA/system.cnf header";
    goto out;
  }
  stage(ui, rep, STAGE_FINISHED);
out:
  source_close(&g_src);
  pfs_umount(W);
  if (rep->err && created) {
    int rrc = 0;
    if (hdd_remove_exact(p->partition, &rrc) != ERR_OK)
      rep->detail = "incomplete PS1 partition could NOT be removed: use Remove Games";
  }
  rep->visible_exists = hdd_exists(p->partition) > 0;
  free(kelf);
  free(jkt_owned);
}

int pops_partition_is_ps1(const char *partition) {
  if (pfs_mount(W, partition, FIO_MT_RDONLY) < 0)
    return 0;
  int yes = file_size(W POPS_IMAGE) > 0;
  pfs_umount(W);
  return yes;
}
