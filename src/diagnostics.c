#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "settings.h"
#include "build_info.h"
#include "diagnostics.h"
#include "hdd_partitions.h"
#include "manifest.h"
#include "network.h"
#include "opl_dependency.h"
#include "opl_launcher_payload.h"
#include "partname.h"
#include "rw_buffer.h"
#include "sha256.h"
#include "ui.h"
#include "util.h"

static char report[6144];
static int roff, nfail;

static void line(const char *status, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void line(const char *status, const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (status && !strcmp(status, "FAIL"))
    nfail++;
  if (roff < (int)sizeof(report) - 1)
    roff += snprintf(report + roff, sizeof(report) - roff, "%-4s %s\n",
                     status ? status : "", buf);
}

/* Compare an embedded blob with the build manifest. */
static void blob_check(const char *label, const char *sym, const void *data,
                       unsigned int size) {
  char hex[65];
  sha256_hex(data, size, hex);
  const build_blob_t *b = build_blob(sym);
  int ok = b && b->size == size && !strcmp(b->sha256, hex);
  line(ok ? "PASS" : "FAIL", "%s %u bytes", label, size);
  line(NULL, "     sha256 %.32s", hex);
  line(NULL, "            %.32s%s", hex + 32, b ? "" : "  (not in manifest)");
}

static void check_build(void) {
  line("INFO", "build %s  variant %s  %s", build_id, build_variant, build_date);
}

static void check_modules(void) {
  for (int i = 0; i < g_app.iop.nmods; i++) {
    line(g_app.iop.mods[i].ok ? "PASS" : "FAIL", "IRX %-12s ret=%d rv=%d",
         g_app.iop.mods[i].module, g_app.iop.mods[i].ret, g_app.iop.mods[i].rv);
    if (!strcmp(g_app.iop.mods[i].module, "ps2hdd_hdl"))
      blob_check("HDD driver ps2hdd-hdl.irx (source-built, patched remove policy)",
                 "ps2hdd_hdl_irx", g_app.iop.mods[i].data, g_app.iop.mods[i].size);
  }
}

static void check_hdd(void) {
  inst_err_t st = g_app.iop.hdd_ok ? hdd_status() : ERR_HDD_MISSING;
  line(st == ERR_HDD_MISSING ? "FAIL" : "PASS", "HDD present");
  line(st == ERR_OK ? "PASS" : "FAIL", "HDD APA formatted");
  uint32_t tot, fr, mx;
  if (st == ERR_OK && hdd_space_mb(&tot, &fr, &mx) == 0) {
    line("INFO", "HDD %lu MiB total, %lu MiB free",
         (unsigned long)tot, (unsigned long)fr);
    line(mx >= 128 ? "PASS" : "FAIL", "max APA partition size %lu MiB", (unsigned long)mx);
  }
}

static void check_app_partition(void) {
  int ex = g_app.iop.hdd_ok ? hdd_exists(INSTALLER_PARTITION) : -1;
  line(ex > 0 ? "PASS" : "FAIL", "%s present", INSTALLER_PARTITION);
  if (g_app.app_rename_rc < 0)
    line("FAIL", "%s could not be renamed (code %d): not shown in the XMB",
         INSTALLER_LEGACY_NAME, g_app.app_rename_rc);
  line(g_app.app_mounted ? "PASS" : "FAIL", "%s mounted read/write at pfs0:",
       g_app.app_rename_rc < 0 ? INSTALLER_LEGACY_NAME : INSTALLER_PARTITION);
  if (!g_app.app_mounted)
    return;
  /* Journal directory writable: write, read back, remove a probe. */
  static const char probe[] = "udpfs-installer diagnostics probe\n";
  char back[64] = "";
  fileXioMkdir(APP_STATE_DIR, 0777);
  int ok = file_write_all(APP_STATE_DIR "/.diag-probe", probe, sizeof(probe) - 1) == 0;
  if (ok) {
    int fd = fileXioOpen(APP_STATE_DIR "/.diag-probe", FIO_O_RDONLY);
    int r = fd >= 0 ? fileXioRead(fd, back, sizeof(back) - 1) : -1;
    if (fd >= 0)
      fileXioClose(fd);
    ok = r == (int)sizeof(probe) - 1 && !memcmp(back, probe, (size_t)r);
  }
  ok = (fileXioRemove(APP_STATE_DIR "/.diag-probe") >= 0) && ok;
  line(ok ? "PASS" : "FAIL", "transaction directory %s writable", APP_STATE_DIR);
}

static void check_network(void) {
  line("INFO", "local IP %s%s", g_app.settings.local_ip,
       g_app.settings.using_default ? " (default)" : "");
  int smap = 0, ms = 0;
  for (int i = 0; i < g_app.iop.nmods; i++) {
    if (!strcmp(g_app.iop.mods[i].module, "smap"))
      smap = g_app.iop.mods[i].ok;
    if (!strcmp(g_app.iop.mods[i].module, "ministack"))
      ms = g_app.iop.mods[i].ok;
  }
  line(smap ? "PASS" : "FAIL", "SMAP driver loaded");
  line(ms ? "PASS" : "FAIL", "ministack loaded (ip=%s)", g_app.settings.local_ip);
  if (g_app.settings.dhcp) {
    char ip[16] = "-";
    if (g_app.iop.ip)
      ip_format(g_app.iop.ip, ip);
    static const char *const st[] = {"not asked", "leased", "no answer, fixed IP used",
                                      "failed, fixed IP used"};
    int s = g_app.iop.dhcp_status >= 0 && g_app.iop.dhcp_status <= 3 ? g_app.iop.dhcp_status : 3;
    line(s == 1 ? "PASS" : "WARN", "DHCP: %s (address in use %s)", st[s], ip);
  } else {
    line("INFO", "DHCP off (ip_mode=static)");
  }
  line(g_app.iop.udpfs_ok ? "PASS" : "FAIL", "UDPFS connected (%s)", net_state_name(g_app.net));
  line(g_app.iop.rw_buffer == RW_BUFFER_FAST ? "PASS" : "WARN",
       "fileXio transfer buffer %d KiB (64 KiB = full-speed install)",
       g_app.iop.rw_buffer / 1024);
  if (g_app.iop.udpfs_ok) {
    int dd = fileXioDopen("udpfs:/"), n = 0;
    if (dd >= 0) {
      iox_dirent_t de;
      while (fileXioDread(dd, &de) > 0)
        n++;
      fileXioDclose(dd);
    }
    line(dd >= 0 ? "PASS" : "FAIL", "udpfs:/ listing (%d entries, code %d)", n,
         dd >= 0 ? 0 : dd);
  }
}

static void check_opl(void) {
  opl_runtime_t opl;
  memset(&opl, 0, sizeof(opl));
  int rc = 0;
  inst_err_t e = g_app.iop.hdd_ok ? opl_check_runtime(&opl, &rc) : ERR_HDD_MISSING;
  line(opl.partition[0] ? "PASS" : "FAIL", "OPL partition resolved: hdd0:%s (%s)",
       opl.partition, opl.from_config ? "conf_hdd.cfg" : "default +OPL");
  line(e == ERR_OK ? "PASS" : "FAIL", "%s exists (code %d)", opl.elf_path, rc);
}

static void check_payloads(void) {
  payload_t k;
  if (payload_opl_launcher(&k, 0) == ERR_OK) {
    blob_check("OPL-Launcher KELF (embedded)", "opl_launcher_kelf", k.data, k.size);
  } else {
    line(!strcmp(build_variant, "dev") ? "WARN" : "FAIL",
         "OPL-Launcher KELF not embedded (%s build)", build_variant);
  }
  payload_release(&k);
  /* udpfsd's copy, used for new channels when it matches the manifest. */
  if (g_app.net == NETWORK_READY && g_manifest_loaded && g_manifest.has_launcher) {
    if (payload_opl_launcher(&k, 1) == ERR_OK && !strcmp(k.origin, "server")) {
      char hex[65];
      sha256_hex(k.data, k.size, hex);
      line("PASS", "OPL-Launcher KELF from server, %lu bytes", (unsigned long)k.size);
      line(NULL, "     sha256 %.32s", hex);
      line(NULL, "            %.32s", hex + 32);
    } else {
      line("WARN", "server OPL-Launcher does not match the manifest;");
      line(NULL, "     channels use the embedded copy");
    }
    payload_release(&k);
  } else {
    line("INFO", "no OPL-Launcher offered by the server; embedded copy used");
  }
  if (g_app.net == NETWORK_READY && g_manifest_loaded && g_manifest.has_opl)
    line("INFO", "server offers OPL %lu bytes (installed if OPL is missing)",
         (unsigned long)g_manifest.opl_size);
  else
    line("INFO", "server offers no OPL runtime (OPNPS2LD.ELF next to udpfsd)");
  if (payload_installer(&k, g_app.app_mounted, 0) == ERR_OK) {
    if (!strcmp(k.origin, "embedded")) {
      blob_check("installer app KELF (embedded)", "installer_kelf", k.data, k.size);
    } else {
      char hex[65];
      sha256_hex(k.data, k.size, hex);
      line("INFO", "installer KELF %s %lu bytes", k.origin, (unsigned long)k.size);
      line(NULL, "     sha256 %.32s", hex);
      line(NULL, "            %.32s", hex + 32);
    }
  } else {
    line("WARN", "installer KELF not available in this run (%s build)", build_variant);
  }
  payload_release(&k);
}

static void run_checks(void) {
  roff = nfail = 0;
  report[0] = 0;
  ui_header("Diagnostics", "Running checks (read-only)...");
  check_build();
  check_modules();
  check_hdd();
  check_app_partition();
  check_network();
  check_opl();
  check_payloads();
  char head[96];
  snprintf(head, sizeof(head), "%d check(s) FAILED", nfail);
  line(nfail ? "FAIL" : "PASS", "%s", nfail ? head : "all checks passed");
  ui_text_view("Pre-hardware diagnostics", report);
}

/* ------------------------------------------------------------------ */
/* HDD self-test: create/format/mount/write/read/unmount/delete a       */
/* temporary 128 MiB PFS partition. Explicit user action only.          */

#define TEST_FILE PFS_WORK "selftest.bin"
#define TEST_SIZE (256 * 1024)

static int list_has(const char *name, int *count) {
  static hdd_part_t parts[256];
  int n = hdd_list(parts, 256), found = 0;
  for (int i = 0; i < n; i++)
    if (!strcmp(parts[i].name, name))
      found = 1;
  if (count)
    *count = n;
  return n < 0 ? -1 : found;
}

static void run_selftest(void) {
  if (!g_app.iop.hdd_ok || hdd_status() != ERR_OK) {
    ui_message("HDD self-test", "HDD not available.");
    return;
  }
  char txt[600];
  snprintf(txt, sizeof(txt),
           "This creates a temporary 128 MiB PFS partition\n\n  %s\n\n"
           "formats and mounts it, writes and verifies a %d KiB test file,\n"
           "unmounts it, deletes it and re-reads the partition table.\n"
           "No other partition is touched.",
           TEST_PARTITION_NAME, TEST_SIZE / 1024);
  if (!ui_confirm_destructive("HDD self-test", txt))
    return;

  roff = nfail = 0;
  report[0] = 0;
  ui_header("HDD self-test", "Working...");
  int before = 0, rc = 0;
  int pre = list_has(TEST_PARTITION_NAME, &before);
  if (pre != 0) {
    line("FAIL", "1 %s already exists (or table unreadable: %d); not touching it",
         TEST_PARTITION_NAME, pre);
    line(NULL, "     use 'Remove leftover PP.UDPFS-TEST' first");
    goto show;
  }
  line("PASS", "1 %s absent, %d partitions", TEST_PARTITION_NAME, before);

  inst_err_t e = pfs_create_partition(TEST_PARTITION_NAME, "128M", &rc);
  line(e ? "FAIL" : "PASS", "2-3 create + format 128M PFS (%s, code %d)", err_name(e), rc);
  if (e)
    goto show;

  uint8_t *buf = memalign(64, TEST_SIZE);
  uint8_t *back = memalign(64, TEST_SIZE);
  int r = pfs_mount(PFS_WORK, TEST_PARTITION_NAME, FIO_MT_RDWR);
  line(r < 0 ? "FAIL" : "PASS", "4 mount at %s (code %d)", PFS_WORK, r);
  if (r >= 0 && buf && back) {
    for (int i = 0; i < TEST_SIZE; i++)
      buf[i] = (uint8_t)(i * 131 + (i >> 9));
    r = file_write_all(TEST_FILE, buf, TEST_SIZE);
    line(r < 0 ? "FAIL" : "PASS", "5 write %d bytes (code %d)", TEST_SIZE, r);
    pfs_umount(PFS_WORK);
    /* Remount so the read really comes from the disk, not a cache. */
    r = pfs_mount(PFS_WORK, TEST_PARTITION_NAME, FIO_MT_RDONLY);
    int fd = r >= 0 ? fileXioOpen(TEST_FILE, FIO_O_RDONLY) : -1;
    int got = 0;
    while (fd >= 0 && got < TEST_SIZE) {
      int n = fileXioRead(fd, back + got, TEST_SIZE - got);
      if (n <= 0)
        break;
      got += n;
    }
    if (fd >= 0)
      fileXioClose(fd);
    line(got == TEST_SIZE && !memcmp(buf, back, TEST_SIZE) ? "PASS" : "FAIL",
         "6 remount + read back %d bytes, compare", got);
  }
  r = fileXioSync(PFS_WORK, 0);
  int u = fileXioUmount(PFS_WORK);
  line(u < 0 ? "FAIL" : "PASS", "7 unmount (sync %d, umount %d)", r, u);
  free(buf);
  free(back);

  e = hdd_remove_exact(TEST_PARTITION_NAME, &rc);
  line(e ? "FAIL" : "PASS", "8 delete %s (%s, code %d)", TEST_PARTITION_NAME,
       err_name(e), rc);
  int after = 0;
  int post = list_has(TEST_PARTITION_NAME, &after);
  line(post == 0 ? "PASS" : "FAIL", "9 deletion confirmed");
  line(after == before ? "PASS" : "FAIL", "10 partition table re-read: %d partitions (was %d)",
       after, before);
show:
  line(nfail ? "FAIL" : "PASS", "%s", nfail ? "self-test FAILED" : "self-test passed");
  ui_text_view("HDD self-test", report);
}

/* Recovery for a PP.UDPFS-TEST left behind by an interrupted self-test.
 * Removal goes through the same typed whitelist (PFS only). */
static void remove_leftover_test(void) {
  int ex = g_app.iop.hdd_ok ? hdd_exists(TEST_PARTITION_NAME) : -1;
  if (ex <= 0) {
    ui_message("HDD self-test", ex == 0 ? "No " TEST_PARTITION_NAME " partition exists."
                                        : "HDD not available.");
    return;
  }
  if (!ui_confirm_destructive("Remove leftover test partition",
                              "Remove the leftover self-test partition\n\n  " TEST_PARTITION_NAME
                              "\n\nNo other partition is touched."))
    return;
  fileXioUmount(PFS_WORK);
  int rc = 0;
  inst_err_t e = hdd_remove_exact(TEST_PARTITION_NAME, &rc);
  char msg[160];
  snprintf(msg, sizeof(msg), "%s (%s, code %d)", e ? "Removal FAILED" : "Removed",
           err_name(e), rc);
  ui_message("HDD self-test", msg);
}

void flow_diagnostics(void) {
  static char rows[3][UI_ROW_LEN] = {
      "Pre-hardware checks (read-only)",
      "HDD self-test: create/delete PP.UDPFS-TEST",
      "Remove leftover PP.UDPFS-TEST",
  };
  for (;;) {
    int c = ui_select("Diagnostics", network_status_line(), rows, 3, 0, NULL, NULL);
    if (c < 0)
      return;
    if (c == 0)
      run_checks();
    else if (c == 1)
      run_selftest();
    else
      remove_leftover_test();
  }
}
