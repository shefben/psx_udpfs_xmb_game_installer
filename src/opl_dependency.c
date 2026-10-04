#include <string.h>

#include "opl_dependency.h"
#include "partname.h"
#include "util.h"

static void set_name(opl_runtime_t *out, const char *name) {
  str_copy(out->partition, name, sizeof(out->partition));
  /* OPL-Launcher: if (oplPartition[5] != '+') prefix = "pfs0:OPL/" */
  str_copy(out->elf_path, name[0] == '+' ? "OPNPS2LD.ELF" : "OPL/OPNPS2LD.ELF",
           sizeof(out->elf_path));
}

int opl_resolve_from_conf(const char *conf_text, opl_runtime_t *out) {
  memset(out, 0, sizeof(*out));
  /* Missing mount, missing file, or fgets()==NULL: fall back to +OPL. */
  if (!conf_text || !conf_text[0]) {
    set_name(out, "+OPL");
    return 0;
  }

  /* fgets(line, 128, fd): first line, at most 127 bytes. */
  char line[128];
  size_t n = 0;
  while (conf_text[n] && conf_text[n] != '\n' && n < sizeof(line) - 1) {
    line[n] = conf_text[n];
    n++;
  }
  line[n] = 0;

  out->from_config = 1;
  char *val = strchr(line, '=');
  if (!val) {
    /* The launcher would call sprintf(name, NULL): undefined. */
    out->config_malformed = 1;
    return -1;
  }
  val++;
  char *cr = strchr(val, '\r');
  if (cr)
    *cr = 0;
  /* The launcher uses the value as a printf format string. */
  if (!val[0] || strchr(val, '%') || strlen(val) > APA_NAME_MAX) {
    out->config_malformed = 1;
    return -1;
  }
  set_name(out, val);
  return 0;
}

#ifdef _EE
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <malloc.h>
#include <stdio.h>

#include "hdd_partitions.h"
#include "manifest.h"
#include "server_assets.h"

inst_err_t opl_check_runtime(opl_runtime_t *out, int *rc_out) {
  static char conf[256];
  int rc = 0, have_conf = 0;
  if (!rc_out)
    rc_out = &rc;
  *rc_out = 0;

  if (pfs_mount(PFS_WORK, "__common", FIO_MT_RDONLY) == 0) {
    int fd = fileXioOpen(PFS_WORK "OPL/conf_hdd.cfg", FIO_O_RDONLY);
    if (fd >= 0) {
      int r = fileXioRead(fd, conf, sizeof(conf) - 1);
      fileXioClose(fd);
      if (r >= 0) {
        conf[r] = 0;
        have_conf = 1;
      }
    }
    pfs_umount(PFS_WORK);
  }
  if (opl_resolve_from_conf(have_conf ? conf : NULL, out) < 0)
    return ERR_OPL_NOT_FOUND;

  int m = pfs_mount(PFS_WORK, out->partition, FIO_MT_RDONLY);
  if (m < 0) {
    *rc_out = m;
    return ERR_OPL_NOT_FOUND;
  }
  char path[64];
  snprintf(path, sizeof(path), PFS_WORK "%s", out->elf_path);
  iox_stat_t st;
  int s = fileXioGetStat(path, &st);
  pfs_umount(PFS_WORK);
  if (s < 0 || st.size == 0) {
    *rc_out = s;
    return ERR_OPL_NOT_FOUND;
  }
  return ERR_OK;
}

/* Install udpfsd's pinned OPNPS2LD.ELF where OPL-Launcher will look for
 * it. Creates only the default "+OPL" (128 MiB PFS, as OPL itself does);
 * never overwrites an existing ELF. Written as .tmp, read back, compared
 * with the manifest's SHA-256, then renamed. */
inst_err_t opl_install_from_server(const char **detail, int *rc_out) {
  int rc = 0;
  if (!rc_out)
    rc_out = &rc;
  *detail = NULL;
  opl_runtime_t opl;
  if (opl_check_runtime(&opl, rc_out) == ERR_OK)
    return ERR_OK;
  if (!g_manifest_loaded || !g_manifest.has_opl) {
    *detail = "the server offers no OPL (OPNPS2LD.ELF next to udpfsd)";
    return ERR_OPL_NOT_FOUND;
  }
  int part_exists = hdd_exists(opl.partition) > 0;
  opl_install_action_t act = opl_install_decide(opl.config_malformed, opl.from_config,
                                                part_exists, 0);
  if (act == OPL_INSTALL_CANNOT) {
    *detail = opl.config_malformed ? "__common/OPL/conf_hdd.cfg is malformed"
                                   : "conf_hdd.cfg names a partition that does not exist";
    return ERR_OPL_NOT_FOUND;
  }

  void *elf = NULL;
  int n = file_load(MANIFEST_DIR "/OPNPS2LD.ELF", &elf, g_manifest.opl_size + 1);
  if (n <= 0 || !blob_matches(elf, (uint32_t)n, g_manifest.opl_sha, g_manifest.opl_size)) {
    free(elf);
    *detail = "server OPNPS2LD.ELF does not match its manifest";
    return ERR_OPL_NOT_FOUND;
  }

  inst_err_t e = ERR_OK;
  if (act == OPL_INSTALL_CREATE_PARTITION &&
      (e = pfs_create_partition(opl.partition, "128M", rc_out)) != ERR_OK) {
    free(elf);
    *detail = "could not create the +OPL partition";
    return e;
  }
  if ((*rc_out = pfs_mount(PFS_WORK, opl.partition, FIO_MT_RDWR)) < 0) {
    free(elf);
    *detail = "could not mount the OPL partition";
    return ERR_PFS_MOUNT;
  }
  char dst[64], tmp[72];
  snprintf(dst, sizeof(dst), PFS_WORK "%s", opl.elf_path);
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  if (strchr(opl.elf_path, '/'))
    fileXioMkdir(PFS_WORK "OPL", 0777);
  iox_stat_t st;
  void *back = NULL;
  e = ERR_OPL_NOT_FOUND;
  if (fileXioGetStat(dst, &st) >= 0 && st.size > 0) {
    *detail = "an OPNPS2LD.ELF exists already (left untouched)"; /* never overwritten */
  } else {
    fileXioRemove(tmp);
    if (file_write_all(tmp, elf, (uint32_t)n) == 0 &&
        file_load(tmp, &back, (uint32_t)n + 1) == n && !memcmp(back, elf, (size_t)n) &&
        (*rc_out = fileXioRename(tmp, dst)) >= 0)
      e = ERR_OK;
    else {
      fileXioRemove(tmp);
      *detail = "writing OPNPS2LD.ELF failed or did not read back identically";
    }
  }
  free(back);
  pfs_umount(PFS_WORK);
  free(elf);
  if (e == ERR_OK && opl_check_runtime(&opl, rc_out) != ERR_OK) {
    *detail = "OPL still not found after installing it";
    e = ERR_OPL_NOT_FOUND;
  }
  return e;
}
#endif
