#ifndef PSXI_APP_CFG_H
#define PSXI_APP_CFG_H

#include <stddef.h>

/* APP.CFG of an app channel: written by the installer (src/apps.c),
 * read by the app launcher (launcher/main.c). Pure: host-tested. */

#define APP_CFG_FILE "APP.CFG"
#define APP_ARGS_MAX 8
#define APP_PATH_MAX 64

typedef struct {
  char boot[APP_PATH_MAX]; /* the ELF inside the partition, e.g. "OPL/OPNPS2LD.ELF" */
  char args[APP_ARGS_MAX][APP_PATH_MAX]; /* argv[1..] */
  int nargs;
} app_cfg_t;

/* APP.CFG: "boot = <path>" (required) and up to APP_ARGS_MAX
 * "arg = <value>" lines; '#' comments, CRLF and unknown keys ignored.
 * 0, or -1 without a valid boot path. `text` need not be NUL-terminated. */
int app_cfg_parse(const char *text, size_t len, app_cfg_t *out);

/* Bytes written (LF line endings), 0 if `outsz` is too small. */
size_t app_cfg_render(const app_cfg_t *c, char *out, size_t outsz);

/* A file path inside the app partition: relative, '/'-separated, no
 * device, "..", backslash, and not one of the channel's own files. */
int app_path_ok(const char *path);

#endif
