#ifndef PSXI_APPS_H
#define PSXI_APPS_H

#include <stddef.h>
#include <stdint.h>

#include "app_cfg.h"
#include "partname.h"

/* Homebrew apps as XMB channels. An app channel is the installer's own
 * channel layout (PFS, res/, header "BOOT2 = pfs:/EXECUTE.KELF") with
 * the signed app launcher as EXECUTE.KELF; the launcher mounts its
 * partition and runs the ELF that APP.CFG names (launcher/main.c).
 * Pure: host-tested, shared with the launcher. */

/* "PP.APPS-<next free number>..<TITLE>" (title as [A-Z0-9_], cut to the
 * 32-byte APA name); numbers of "PP.APPS-" names in `names` are
 * skipped. 0, or -1 if every number is taken. */
int app_partition_name(const char *title, const char *const *names, int n,
                       char out[APA_NAME_MAX + 1]);

/* PFS partition size for `content_mb` of app files plus 16 MiB for PFS
 * and res/: "128M", "256M", "512M", "1G" or "2G" (*size_mb set); NULL if
 * the app is bigger than that. */
const char *app_size_str(uint64_t content_mb, uint32_t *size_mb);

/* XMB title from a file or folder name: extension dropped, '_' as space;
 * "App" when nothing is left. */
void app_title_from_name(const char *name, char *out, size_t outsz);

#endif
