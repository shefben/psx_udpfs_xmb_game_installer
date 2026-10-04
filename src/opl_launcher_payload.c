#include <malloc.h>
#include <string.h>

#include "hdd_partitions.h"
#include "hdl_header.h"
#include "manifest.h"
#include "opl_launcher_payload.h"
#include "server_assets.h"

#define KELF_MAX (16 * 1024 * 1024)

#if !defined(VARIANT_APP) && !defined(VARIANT_BOOTSTRAP) && !defined(VARIANT_DEV)
#error "build with -DVARIANT_APP, -DVARIANT_BOOTSTRAP or -DVARIANT_DEV"
#endif
#if (defined(VARIANT_APP) || defined(VARIANT_BOOTSTRAP)) && !defined(HAVE_EMBEDDED_OPL_LAUNCHER)
#error "release variants must embed the signed OPL-Launcher KELF"
#endif
#if defined(VARIANT_BOOTSTRAP) && !defined(HAVE_EMBEDDED_INSTALLER_KELF)
#error "the bootstrap variant must embed the signed installer app KELF"
#endif

extern unsigned char default_jkt_png[];
extern unsigned int size_default_jkt_png;
extern unsigned char installer_jkt_png[];
extern unsigned int size_installer_jkt_png;

#ifdef HAVE_EMBEDDED_OPL_LAUNCHER
extern unsigned char opl_launcher_kelf[];
extern unsigned int size_opl_launcher_kelf;
#endif
#ifdef HAVE_EMBEDDED_INSTALLER_KELF
extern unsigned char installer_kelf[];
extern unsigned int size_installer_kelf;
#endif

const char *payload_build_variant(void) {
#if defined(VARIANT_BOOTSTRAP)
  return "bootstrap";
#elif defined(VARIANT_APP)
  return "app";
#else
  return "dev";
#endif
}

int payload_opl_launcher_embedded(void) {
#ifdef HAVE_EMBEDDED_OPL_LAUNCHER
  return 1;
#else
  return 0;
#endif
}

int payload_installer_embedded(void) {
#ifdef HAVE_EMBEDDED_INSTALLER_KELF
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

__attribute__((unused)) static int use_embedded(payload_t *out, const uint8_t *data, uint32_t size) {
  if (!kelf_looks_valid(data, size))
    return 0;
  out->data = data;
  out->size = size;
  out->origin = "embedded";
  return 1;
}

__attribute__((unused)) static int try_file(payload_t *out, const char *path) {
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

inst_err_t payload_opl_launcher(payload_t *out, int udpfs_ok) {
  memset(out, 0, sizeof(*out));
  /* udpfsd's copy (udpfsd.cfg opl_launcher), only when its bytes match
   * the size and SHA-256 the manifest announced; else the embedded one. */
  if (udpfs_ok && g_manifest_loaded && g_manifest.has_launcher) {
    void *buf = NULL;
    int n = file_load(MANIFEST_DIR "/EXECUTE.KELF", &buf, KELF_MAX);
    if (n > 0 && launcher_copy_valid(buf, (uint32_t)n, g_manifest.launcher_sha,
                                     g_manifest.launcher_size)) {
      out->data = buf;
      out->size = (uint32_t)n;
      out->owned = 1;
      out->origin = "server";
      return ERR_OK;
    }
    free(buf);
  }
#ifdef HAVE_EMBEDDED_OPL_LAUNCHER
  if (use_embedded(out, opl_launcher_kelf, size_opl_launcher_kelf))
    return ERR_OK;
#endif
#ifdef VARIANT_DEV
  if (udpfs_ok && try_file(out, "udpfs:/PAYLOAD/opl-launcher-EXECUTE.KELF"))
    return ERR_OK;
#endif
  return ERR_KELF_MISSING;
}

inst_err_t payload_installer(payload_t *out, int app_mounted, int udpfs_ok) {
  memset(out, 0, sizeof(*out));
  (void)app_mounted;
  (void)udpfs_ok;
#ifdef HAVE_EMBEDDED_INSTALLER_KELF
  if (use_embedded(out, installer_kelf, size_installer_kelf))
    return ERR_OK;
#endif
#ifdef VARIANT_APP
  /* Repair in place from the KELF this app was launched from. */
  if (app_mounted && try_file(out, PFS_APP "EXECUTE.KELF"))
    return ERR_OK;
#endif
#ifdef VARIANT_DEV
  if (udpfs_ok && try_file(out, "udpfs:/PAYLOAD/installer-EXECUTE.KELF"))
    return ERR_OK;
#endif
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
