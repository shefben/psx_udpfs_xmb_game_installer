#include <stdio.h>
#include <string.h>
#include <time.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "backup.h"
#include "browser.h"
#include "crc32.h"
#include "hdd_partitions.h"
#include "hdl_install.h"
#include "iso9660.h"
#include "pops.h"
#include "remove_games.h"
#include "transaction.h"
#include "util.h"

#define BK_BUF (1024 * 1024)
static uint8_t bk_buf[BK_BUF] __attribute__((aligned(64)));
static char dest[SOURCE_PATH_MAX];

static void stage(const install_ui_t *ui, install_report_t *rep, install_stage_t s) {
  rep->stage = s;
  if (ui && ui->stage)
    ui->stage(ui->ctx, s);
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

/* First free name (" (2)", " (3)" ...); also creates the folder. */
static int pick_dest(int dvd, const char *boot_id, const char *title, const char *ext) {
  char dir[32];
  snprintf(dir, sizeof(dir), "%s%s", USB_ROOT, dvd < 0 ? "POPS" : dvd ? "DVD" : "CD");
  fileXioMkdir(dir, 0777);
  for (int n = 0; n < 100; n++) {
    if (backup_path(dest, sizeof(dest), USB_ROOT, dvd, boot_id, title, n ? n + 1 : 0, ext) < 0)
      return -1;
    if (file_size(dest) < 0)
      return 0;
  }
  return -1;
}

/* Copy `total` bytes from the open fd `in` to a new file `dest`; CRC-32
 * of the bytes read. */
static inst_err_t copy_out(int in, uint64_t total, const install_ui_t *ui, install_report_t *rep) {
  int out = fileXioOpen(dest, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (out < 0) {
    rep->rc = out;
    rep->detail = "cannot create the file on the USB drive";
    return ERR_SOURCE_OPEN;
  }
  inst_err_t e = ERR_OK;
  uint64_t done = 0;
  uint32_t crc = 0;
  time_t start = time(NULL), last = 0;
  int abort = 0;
  while (done < total) {
    uint32_t want = total - done > BK_BUF ? BK_BUF : (uint32_t)(total - done);
    int r = fileXioRead(in, bk_buf, (int)want);
    if (r != (int)want) {
      rep->rc = r;
      rep->detail = "HDD read";
      e = ERR_HDL_VERIFY;
      break;
    }
    crc = crc32_update(crc, bk_buf, want);
    int w = fileXioWrite(out, bk_buf, (int)want);
    if (w != (int)want) {
      rep->rc = w;
      rep->detail = done + want > 0xFFFFFFFFull
                        ? "USB write failed past 4 GiB: format the drive as exFAT"
                        : "USB write failed (drive full?)";
      e = ERR_SOURCE_READ;
      break;
    }
    done += want;
    progress(ui, start, &last, done, total, &abort);
    if (abort) {
      e = ERR_USER_ABORT;
      break;
    }
  }
  if (fileXioClose(out) < 0 && !e) {
    rep->detail = "USB close";
    e = ERR_SOURCE_READ;
  }
  rep->bytes_written = done;
  rep->source_crc32 = crc;
  return e;
}

/* Read the USB file back; ERR_USER_ABORT + skip flag = skipped. */
static inst_err_t verify_usb(uint64_t total, const install_ui_t *ui, install_report_t *rep) {
  int fd = fileXioOpen(dest, FIO_O_RDONLY);
  if (fd < 0) {
    rep->rc = fd;
    return ERR_XMB_VERIFY;
  }
  inst_err_t e = ERR_OK;
  uint64_t done = 0;
  uint32_t crc = 0;
  time_t start = time(NULL), last = 0;
  int abort = 0;
  while (done < total) {
    uint32_t want = total - done > BK_BUF ? BK_BUF : (uint32_t)(total - done);
    int r = fileXioRead(fd, bk_buf, (int)want);
    if (r != (int)want) {
      rep->rc = r;
      e = ERR_XMB_VERIFY;
      break;
    }
    crc = crc32_update(crc, bk_buf, want);
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
      rep->detail = "the file on the USB drive differs from the HDD data";
      e = ERR_XMB_VERIFY;
    }
  }
  if (e == ERR_USER_ABORT && ui && ui->skip_verify && ui->skip_verify(ui->ctx)) {
    rep->verify_skipped = 1;
    e = ERR_OK;
  }
  return e;
}

static void finish(install_report_t *rep) {
  if (rep->err) {
    if (dest[0])
      fileXioRemove(dest); /* never leave a partial copy */
  } else {
    rep->detail = dest;
  }
}

void backup_ps2_game(const char *hidden, const install_ui_t *ui, install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  dest[0] = 0;
  stage(ui, rep, STAGE_PREPARING);
  hdl_header_info_t h;
  char boot_id[16];
  if (hdl_read_header(hidden, &h) < 0 || part_id_from_partition(hidden, boot_id) < 0 ||
      h.data_bytes == 0 || h.data_bytes % 2048) {
    rep->err = ERR_HDL_VERIFY;
    rep->detail = "game header unreadable";
    return;
  }
  /* The exact game size: the journal's, else the header's (hdlfs stores
   * the ISO size there). */
  uint64_t total = h.data_bytes;
  tx_journal_t j;
  int have_j = g_app.app_mounted && tx_load(APP_STATE_DIR, hidden, &j) == ERR_OK &&
               !strcmp(j.hidden_partition, hidden) && j.bytes_expected;
  if (have_j) {
    /* Only a finished copy: an interrupted one (resume pending) would back
     * up stale sectors that its own read-back cannot tell apart. */
    tx_state_t s = j.state == TX_FAILED ? j.failed_from : j.state;
    if (s < TX_HDL_COMPLETE || !j.has_source_crc || j.bytes_written != j.bytes_expected) {
      rep->err = ERR_HDL_VERIFY;
      rep->detail = "the copy of this game never finished: Resume or reinstall it first";
      return;
    }
    total = j.bytes_expected;
  }
  if (pick_dest(h.disc_type == DISC_TYPE_DVD, boot_id, h.title, ".iso") < 0) {
    rep->err = ERR_INVALID_ARG;
    rep->detail = "no free file name on the USB drive";
    return;
  }
  char dev[48];
  snprintf(dev, sizeof(dev), "hdd0:%s", hidden);
  fileXioUmount("hdl0:");
  int r = fileXioMount("hdl0:", dev, FIO_MT_RDONLY);
  int fd = r < 0 ? r : fileXioOpen("hdl0:", FIO_O_RDONLY);
  if (fd < 0) {
    rep->err = ERR_HDL_MOUNT;
    rep->rc = fd;
    fileXioUmount("hdl0:");
    dest[0] = 0;
    return;
  }
  stage(ui, rep, STAGE_COPYING);
  fileXioLseek(fd, 0, FIO_SEEK_SET); /* hdlfs: 2048-byte sectors */
  rep->err = copy_out(fd, total, ui, rep);
  fileXioClose(fd);
  fileXioUmount("hdl0:");
  if (!rep->err && have_j && j.has_source_crc && j.bytes_expected == total &&
      rep->source_crc32 != j.source_crc32) {
    rep->err = ERR_HDL_VERIFY;
    rep->detail = "the HDD data no longer matches its install CRC: not backed up";
  }
  if (!rep->err) {
    stage(ui, rep, STAGE_VALIDATING);
    rep->err = verify_usb(total, ui, rep);
  }
  finish(rep);
}

void backup_ps1_game(const char *partition, const install_ui_t *ui, install_report_t *rep) {
  memset(rep, 0, sizeof(*rep));
  dest[0] = 0;
  stage(ui, rep, STAGE_PREPARING);
  char boot_id[16];
  if (part_id_from_partition(partition, boot_id) < 0) {
    rep->err = ERR_INVALID_ARG;
    return;
  }
  const char *title = partition + 3 + PART_ID_LEN + 2;
  if (pick_dest(-1, boot_id, strlen(partition) > 3 + PART_ID_LEN + 2 ? title : "", ".VCD") < 0) {
    rep->err = ERR_INVALID_ARG;
    rep->detail = "no free file name on the USB drive";
    return;
  }
  int r = pfs_mount(PFS_WORK, partition, FIO_MT_RDONLY);
  int64_t size = r < 0 ? -1 : file_size(PFS_WORK POPS_IMAGE);
  int fd = size > 0 ? fileXioOpen(PFS_WORK POPS_IMAGE, FIO_O_RDONLY) : -1;
  if (fd < 0) {
    rep->err = ERR_PFS_MOUNT;
    rep->rc = r < 0 ? r : fd;
    pfs_umount(PFS_WORK);
    dest[0] = 0;
    return;
  }
  stage(ui, rep, STAGE_COPYING);
  rep->err = copy_out(fd, (uint64_t)size, ui, rep);
  fileXioClose(fd);
  pfs_umount(PFS_WORK);
  if (!rep->err) {
    stage(ui, rep, STAGE_VALIDATING);
    rep->err = verify_usb((uint64_t)size, ui, rep);
  }
  finish(rep);
}
