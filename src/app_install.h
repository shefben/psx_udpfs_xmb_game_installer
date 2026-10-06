#ifndef PSXI_APP_INSTALL_H
#define PSXI_APP_INSTALL_H

#include <stdint.h>

#include "apps.h"
#include "errors.h"
#include "source.h"

/* Install a homebrew ELF (from udpfs:/APPS or USB) as an XMB app
 * channel: a PFS "PP.APPS-NNNNN..TITLE" partition with the app launcher
 * as EXECUTE.KELF, res/ (app icon, title), the ELF (and, if asked, every
 * file of its folder) and APP.CFG. Copied, read back by CRC-32, and
 * removed again on any failure. */

#define APP_FILES_MAX 256

typedef struct {
  char rel[APP_PATH_MAX]; /* path inside the partition */
  uint64_t size;
} app_file_t;

typedef struct {
  char elf[SOURCE_PATH_MAX]; /* the chosen ELF */
  char dir[SOURCE_PATH_MAX]; /* its folder */
  int with_folder;           /* copy every file of the folder (subfolders too) */
  char title[48];            /* XMB title */
  app_file_t files[APP_FILES_MAX];
  int nfiles;
  int skipped;               /* files left out (names, depth, count) */
  int boot;                  /* index of the ELF in files[] */
  uint64_t bytes;
} app_plan_t;

typedef struct {
  inst_err_t err;
  int rc;
  const char *step;
  char partition[APA_NAME_MAX + 1];
} app_report_t;

/* List what would be copied. ERR_OK, ERR_SOURCE_OPEN or ERR_INVALID_ARG. */
inst_err_t app_plan_build(const char *elf_path, int with_folder, app_plan_t *p);

/* Called per file (index, count, its path) while copying and verifying. */
typedef void (*app_progress_fn)(const char *stage, int i, int n, const char *rel);

void app_install(const app_plan_t *p, app_progress_fn progress, app_report_t *rep);

#endif
