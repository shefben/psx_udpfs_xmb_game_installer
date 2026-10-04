#ifndef PSXI_XMB_INSTALLER_APP_H
#define PSXI_XMB_INSTALLER_APP_H

#include "errors.h"

/* Install/Repair the installer's own XMB channel PP.UDPFS-INSTALLER
 * (plan sections 21, 34). Keeps existing config/ and state/. */

typedef struct {
  inst_err_t err;
  int rc;
  const char *detail;
  const char *kelf_origin;
  int created; /* 1 if the partition was newly created */
  int opl_launcher_stashed; /* payload/OPL-LAUNCHER.KELF written */
} selfinstall_report_t;

void installer_app_install(selfinstall_report_t *rep);

/* Verify-only (diagnostics). */
inst_err_t installer_app_verify(const char **detail);

#endif
