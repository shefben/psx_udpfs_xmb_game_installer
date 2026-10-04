#include <malloc.h>
#include <string.h>

#include "hdd_partitions.h"
#include "hdl_header.h"
#include "opl_launcher_payload.h"

#define KELF_MAX (4 * 1024 * 1024)

extern unsigned char default_jkt_png[];
extern unsigned int size_default_jkt_png;
extern unsigned char installer_jkt_png[];
extern unsigned int size_installer_jkt_png;

#ifdef HAVE_EMBEDDED_OPL_LAUNCHER
extern unsigned char opl_launcher_kelf[];
extern unsigned int size_opl_launcher_kelf;
#endif

int payload_opl_launcher_embedded(void) {
#ifdef HAVE_EMBEDDED_OPL_LAUNCHER
  return 1;
#else
  return 0;
#endif
}

void payload_release(payload_t *p) {
  if (p->owned)
    free((void *)p->data);
  memset(p, 0, sizeof(*p));
}

static int try_file(payload_t *out, const char *path) {
  void *buf = NULL;
  int n = file_load(path, &buf, KELF_MAX);
  if (n <= 0)
    return 0;
  if (!kelf_looks_valid(buf, (uint32_t)n)) {
    free(buf);
    return 0;
  }
  out->data = buf;
  out->size = (uint32_t)n;
  out->owned = 1;
  out->origin = path;
  return 1;
}

inst_err_t payload_opl_launcher(payload_t *out, int app_mounted, int udpfs_ok) {
  memset(out, 0, sizeof(*out));
#ifdef HAVE_EMBEDDED_OPL_LAUNCHER
  if (kelf_looks_valid(opl_launcher_kelf, size_opl_launcher_kelf)) {
    out->data = opl_launcher_kelf;
    out->size = size_opl_launcher_kelf;
    out->origin = "embedded";
    return ERR_OK;
  }
#endif
  if (app_mounted && try_file(out, PFS_APP "payload/OPL-LAUNCHER.KELF"))
    return ERR_OK;
  if (udpfs_ok && try_file(out, "udpfs:/PAYLOAD/opl-launcher-EXECUTE.KELF"))
    return ERR_OK;
  return ERR_KELF_MISSING;
}

inst_err_t payload_installer(payload_t *out, int app_mounted, int udpfs_ok) {
  memset(out, 0, sizeof(*out));
  if (udpfs_ok && try_file(out, "udpfs:/PAYLOAD/installer-EXECUTE.KELF"))
    return ERR_OK;
  if (app_mounted && try_file(out, PFS_APP "EXECUTE.KELF"))
    return ERR_OK;
  return ERR_KELF_MISSING;
}

void payload_default_jacket(const uint8_t **data, uint32_t *size) {
  *data = default_jkt_png;
  *size = size_default_jkt_png;
}

void payload_installer_jacket(const uint8_t **data, uint32_t *size) {
  *data = installer_jkt_png;
  *size = size_installer_jkt_png;
}
