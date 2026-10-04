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
#include <stdio.h>

#include "hdd_partitions.h"

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
#endif
