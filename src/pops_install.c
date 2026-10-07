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
#include "extras_install.h"
#include "hdd_partitions.h"
#include "manifest.h"
#include "pfs_channel.h"
#include "pops_install.h"
#include "source_udpfs.h"
#include "source_wire.h"
#include "xmb_game_channel.h"
#include "util.h"
#include "xmb_text.h"

#define W PFS_WORK
#define POPS_BUF (512 * 1024)
#define POPS_KELF_MAX (2 * 1024 * 1024)
#define POPS_FILE_MAX (8 * 1024 * 1024)

static uint8_t buf[POPS_BUF] __attribute__((aligned(64)));
static GameSource g_src;
static udpfs_src_t g_u;
static wire_src_t g_w;

static inst_err_t open_source(const char *path);

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
  inst_err_t e = open_source(path);
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
  p->ndiscs = 1;
  str_copy(p->disc_path[0], path, SOURCE_PATH_MAX);
  p->disc_bytes[0] = p->vcd.bytes;
  p->size_mb = pops_partition_mb(p->vcd.bytes, p->size_str, sizeof(p->size_str));
  return p->size_mb < 0 ? ERR_HDL_PLAN : ERR_OK;
}

/* <folder of the VCD>/<name>, else <device>/POPS/<name>. Probed by
 * opening the file: udpfsd answers getstat only for open files, so a
 * getstat probe reported POPSTARTER.KELF etc. missing on the server. */
static int find_pops_file(const char *vcd_path, const char *name, char *out, size_t sz) {
  const char *slash = strrchr(vcd_path, '/');
  if (slash) {
    snprintf(out, sz, "%.*s/%s", (int)(slash - vcd_path), vcd_path, name);
    if (file_open_size(out) > 0)
      return 0;
  }
  const char *colon = strchr(vcd_path, ':');
  if (colon) {
    snprintf(out, sz, "%.*s:/POPS/%s", (int)(colon - vcd_path), vcd_path, name);
    if (file_open_size(out) > 0)
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
    /* Multi-disc: DISCS.TXT in the game's folder (POPStarter docs). */
    char discs[96], dpath[96];
    int dlen = p->ndiscs > 1 ? pops_discs_txt(p->ndiscs, discs, sizeof(discs)) : 0;
    snprintf(dpath, sizeof(dpath), "%s/" POPS_DISCS, dir);
    if (dlen > 0 && (r = file_write_all(dpath, discs, (uint32_t)dlen)) < 0) {
      rep->rc = r;
      rep->detail = "__common/POPS/<game>/DISCS.TXT";
      e = ERR_XMB_RESOURCE_WRITE;
    }
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

/* The server's LZ4 frames when it offers them (only compressed bytes
 * cross the network), else plain reads. */
static inst_err_t open_source(const char *path) {
  if (game_source_is_server(path) && g_manifest_loaded && g_manifest.wire_lz4f) {
    source_wire_init(&g_src, &g_w);
    if (source_open(&g_src, path) == ERR_OK)
      return ERR_OK;
  }
  source_udpfs_init(&g_src, &g_u);
  return source_open(&g_src, path);
}

int pops_plan_find_discs(pops_plan_t *p) {
  char stem[96], dir[SOURCE_PATH_MAX];
  int mine = pops_disc_number(p->source_path, stem, sizeof(stem));
  if (mine <= 0)
    return 1;
  const char *slash = strrchr(p->source_path, '/');
  if (!slash)
    return 1;
  snprintf(dir, sizeof(dir), "%.*s", (int)(slash - p->source_path), p->source_path);
  static char found[POPS_MAX_DISCS][SOURCE_PATH_MAX];
  memset(found, 0, sizeof(found));
  int dd = fileXioDopen(dir);
  if (dd < 0)
    return 1;
  iox_dirent_t de;
  while (fileXioDread(dd, &de) > 0) {
    char s[96];
    if (source_classify(de.name) != SRC_TYPE_VCD)
      continue;
    int d = pops_disc_number(de.name, s, sizeof(s));
    if (d >= 1 && !strcasecmp(s, stem) && !found[d - 1][0] &&
        strlen(dir) + 1 + strlen(de.name) < SOURCE_PATH_MAX) {
      strcpy(found[d - 1], dir);
      strcat(found[d - 1], "/");
      strcat(found[d - 1], de.name);
    }
  }
  fileXioDclose(dd);
  int n = 0;
  while (n < POPS_MAX_DISCS && found[n][0])
    n++;
  if (n < 2 || n < mine)
    return 1;
  /* Every disc must be a readable VCD; disc 1 names the game. */
  static pops_plan_t q;
  q = *p;
  for (int i = 0; i < n; i++) {
    vcd_info_t v;
    inst_err_t e = open_source(found[i]);
    if (!e)
      e = vcd_probe(&g_src, &v);
    source_close(&g_src);
    if (e)
      return 1;
    if (i == 0)
      q.vcd = v;
    str_copy(q.disc_path[i], found[i], SOURCE_PATH_MAX);
    q.disc_bytes[i] = v.bytes;
  }
  uint64_t total = 0;
  for (int i = 0; i < n; i++)
    total += q.disc_bytes[i];
  q.ndiscs = n;
  str_copy(q.source_path, found[0], sizeof(q.source_path));
  if (pops_plan_set_title(&q, stem) != ERR_OK)
    return 1;
  q.size_mb = pops_partition_mb(total, q.size_str, sizeof(q.size_str));
  if (q.size_mb < 0)
    return 1;
  *p = q;
  return n;
}

/* Disc i of the source -> W IMAGE<i>.VCD, CRC-32 of every byte. done and
 * total count over all discs (progress). */
static inst_err_t copy_disc(const pops_plan_t *p, int i, const install_ui_t *ui,
                            install_report_t *rep, uint64_t *done, uint64_t total,
                            uint32_t *crc_out) {
  char dst[32];
  snprintf(dst, sizeof(dst), W "%s", pops_image_name(i));
  inst_err_t e = open_source(p->disc_path[i]);
  if (e) {
    rep->rc = g_src.last_rc;
    return e;
  }
  int fd = fileXioOpen(dst, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (fd < 0) {
    rep->rc = fd;
    source_close(&g_src);
    return ERR_XMB_RESOURCE_WRITE;
  }
  uint64_t size = p->disc_bytes[i], got = 0;
  uint32_t crc = 0;
  time_t start = time(NULL), last = 0;
  int abort = 0;
  if (g_src.ops->seek(&g_src, 0, SRC_SEEK_SET) != 0 || source_size(&g_src) != (int64_t)size)
    e = ERR_SOURCE_READ;
  while (!e && got < size) {
    uint32_t want = size - got > POPS_BUF ? POPS_BUF : (uint32_t)(size - got);
    const uint8_t *blk;
    uint32_t n;
    if ((e = source_next_block(&g_src, buf, want, &blk, &n))) {
      rep->rc = g_src.last_rc;
      break;
    }
    /* A lent block is only valid until the next source call: write it
     * from buf, which is 64-byte aligned for fileXio. */
    if (blk != buf)
      memcpy(buf, blk, n);
    crc = crc32_update(crc, buf, n);
    int w = fileXioWrite(fd, buf, (int)n);
    if (w != (int)n) {
      rep->rc = w;
      e = ERR_XMB_RESOURCE_WRITE;
      break;
    }
    got += n;
    *done += n;
    progress(ui, start, &last, *done, total, &abort);
    if (abort)
      e = ERR_USER_ABORT;
  }
  if (fileXioClose(fd) < 0 && !e)
    e = ERR_XMB_RESOURCE_WRITE;
  source_close(&g_src);
  *crc_out = crc;
  return e;
}

/* Read W IMAGE<i>.VCD back and compare with its copy CRC. */
static inst_err_t verify_disc(const pops_plan_t *p, int i, const install_ui_t *ui,
                              install_report_t *rep, uint64_t *done, uint64_t total,
                              uint32_t want_crc) {
  char path[32];
  snprintf(path, sizeof(path), W "%s", pops_image_name(i));
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0) {
    rep->rc = fd;
    return ERR_XMB_VERIFY;
  }
  inst_err_t e = ERR_OK;
  uint64_t size = p->disc_bytes[i], got = 0;
  uint32_t crc = 0;
  time_t start = time(NULL), last = 0;
  int abort = 0;
  while (got < size) {
    uint32_t want = size - got > POPS_BUF ? POPS_BUF : (uint32_t)(size - got);
    int r = fileXioRead(fd, buf, (int)want);
    if (r != (int)want) {
      rep->rc = r;
      e = ERR_XMB_VERIFY;
      break;
    }
    crc = crc32_update(crc, buf, want);
    got += want;
    *done += want;
    progress(ui, start, &last, *done, total, &abort);
    if (abort) {
      e = ERR_USER_ABORT;
      break;
    }
  }
  fileXioClose(fd);
  if (!e && crc != want_crc) {
    rep->detail = "CRC-32 of a VCD on the HDD != source";
    e = ERR_XMB_VERIFY;
  }
  if (!e && i == 0) {
    rep->have_crc = 1;
    rep->installed_crc32 = crc;
  }
  return e;
}

static void stage(const install_ui_t *ui, install_report_t *rep, install_stage_t s) {
  rep->stage = s;
  if (ui && ui->stage)
    ui->stage(ui->ctx, s);
}

/* Memory cards, saves and cheats from the server's VMC / CHT folders;
 * best effort, after the game is complete. */
static const char *ps1_extras(const pops_plan_t *p) {
  static char text[220];
  extras_opts_t o = {0, 0};
  extras_report_t r;
  extras_install_ps1(p->partition, p->vcd.boot_id, &o, &r);
  if (!r.found)
    return NULL;
  snprintf(text, sizeof(text), "%.80s%s%.80s", r.installed ? r.what : "already there",
           r.failed ? "; not all: " : "", r.failed ? r.note : "");
  return text;
}

void pops_install(pops_plan_t *p, const install_ui_t *ui, install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  void *kelf = NULL, *jkt_owned[2] = {NULL, NULL};
  int created = 0;
  uint32_t crc[POPS_MAX_DISCS] = {0};
  stage(ui, rep, STAGE_PREPARING);

  if (p->ndiscs < 1 || p->ndiscs > POPS_MAX_DISCS) {
    rep->err = ERR_INVALID_ARG;
    goto out;
  }
  if (game_source_is_server(p->source_path) && g_app.net != NETWORK_READY) {
    rep->err = ERR_NETWORK;
    goto out;
  }
  uint64_t total = 0;
  for (int i = 0; i < p->ndiscs; i++) {
    vcd_info_t again;
    if ((rep->err = open_source(p->disc_path[i]))) {
      rep->rc = g_src.last_rc;
      goto out;
    }
    rep->err = vcd_probe(&g_src, &again);
    source_close(&g_src);
    if (rep->err)
      goto out;
    if (again.bytes != p->disc_bytes[i] || (i == 0 && strcmp(again.boot_id, p->vcd.boot_id))) {
      rep->err = ERR_SOURCE_INVALID_ISO;
      rep->detail = "source changed since it was selected";
      goto out;
    }
    total += p->disc_bytes[i];
  }
  if (hdd_exists(p->partition) != 0) {
    rep->err = ERR_PARTITION_EXISTS;
    goto out;
  }
  if ((rep->err = hdd_space_check((uint32_t)p->size_mb))) /* incl. the 128 GiB limit */
    goto out;
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
  /* Multi-disc: DISCS.TXT next to the VCDs too (PSX-XMB-Manager's place;
   * install_runtime wrote the documented one in the game's POPS folder). */
  char discs[96];
  int dlen = p->ndiscs > 1 ? pops_discs_txt(p->ndiscs, discs, sizeof(discs)) : 0;
  if (dlen > 0 && (r = file_write_all(W POPS_DISCS, discs, (uint32_t)dlen)) < 0) {
    rep->err = ERR_XMB_RESOURCE_WRITE;
    rep->rc = r;
    rep->detail = POPS_DISCS;
    goto out;
  }
  char info[1024], today[9];
  xmb_game_info_t gi;
  int have_gi = game_load_info(p->vcd.boot_id, &gi);
  install_date(today);
  uint32_t info_len = (uint32_t)xmb_game_info_sys_ex(info, sizeof(info), p->title, p->vcd.boot_id,
                                                     have_gi ? &gi : NULL, today);
  jacket_pair_t jkt;
  rep->jacket = game_load_jackets(p->vcd.boot_id, &jkt, jkt_owned);
  channel_result_t cr = info_len ? channel_write_res(p->title, info, info_len, &jkt)
                                 : (channel_result_t){ERR_XMB_RESOURCE_WRITE, 0, "res/info.sys"};
  if (cr.err) {
    rep->err = cr.err;
    rep->rc = cr.rc;
    rep->detail = cr.step;
    goto out;
  }
  stage(ui, rep, STAGE_COPYING);
  uint64_t done = 0;
  for (int i = 0; i < p->ndiscs; i++)
    if ((rep->err = copy_disc(p, i, ui, rep, &done, total, &crc[i])))
      goto out;
  rep->bytes_written = done;
  rep->source_crc32 = crc[0];
  pfs_umount(W);

  stage(ui, rep, STAGE_VALIDATING);
  if ((r = pfs_mount(W, p->partition, FIO_MT_RDONLY)) < 0) {
    rep->err = ERR_PFS_MOUNT;
    rep->rc = r;
    goto out;
  }
  if (file_size(W "EXECUTE.KELF") != klen || file_size(W "res/info.sys") != info_len ||
      (dlen > 0 && file_size(W POPS_DISCS) != dlen)) {
    rep->err = ERR_XMB_VERIFY;
    rep->detail = "EXECUTE.KELF / res/info.sys / DISCS.TXT";
    goto out;
  }
  done = 0;
  for (int i = 0; i < p->ndiscs && !rep->err; i++)
    rep->err = verify_disc(p, i, ui, rep, &done, total, crc[i]);
  rep->bytes_verified = done;
  if (rep->err == ERR_USER_ABORT && ui && ui->skip_verify && ui->skip_verify(ui->ctx)) {
    rep->verify_skipped = 1; /* copied in full with its CRC; read-back skipped */
    rep->err = ERR_OK;
  }
  if (rep->err)
    goto out;
  pfs_umount(W);

  /* Last: the header that makes the channel appear in the XMB. */
  stage(ui, rep, STAGE_CREATING_CHANNEL);
  char part_id[PART_ID_LEN + 1] = "";
  boot_id_to_part_id(p->vcd.boot_id, part_id);
  if ((rep->err = osd_header_write(p->partition, XMB_SYSTEM_CNF, p->title, part_id,
                                   &rep->rc))) {
    rep->detail = "OSD header (system.cnf, icon.sys, icon)";
    goto out;
  }
  rep->extras = ps1_extras(p);
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
  free(jkt_owned[0]);
  free(jkt_owned[1]);
}

int pops_partition_is_ps1(const char *partition) {
  if (pfs_mount(W, partition, FIO_MT_RDONLY) < 0)
    return 0;
  int yes = file_size(W POPS_IMAGE) > 0;
  pfs_umount(W);
  return yes;
}
