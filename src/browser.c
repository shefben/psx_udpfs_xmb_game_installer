#include <stdio.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#include "app_state.h"
#include "browser.h"
#include "flows.h"
#include "listui.h"
#include "network.h"
#include "source.h"
#include "ui.h"
#include "util.h"

#define MAX_ENTRIES 256
#define NAME_MAX_ 128

typedef struct {
  char name[NAME_MAX_];
  int is_dir;
  source_type_t type;
  uint64_t size;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static char rows[MAX_ENTRIES][UI_ROW_LEN];
static lv_item_t items[MAX_ENTRIES];
static listui_state_t ls; /* order and search kept while browsing */

static int list_dir(const char *dir, int *count) {
  *count = 0;
  int dd = fileXioDopen(dir);
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  int r, n = 0;
  while (n < MAX_ENTRIES && (r = fileXioDread(dd, &de)) > 0) {
    if (de.name[0] == '.') /* ".", "..", udpfsd's .udpfsd data */
      continue;
    int is_dir = (de.stat.mode & FIO_S_IFMT) == FIO_S_IFDIR;
    source_type_t t = is_dir ? SRC_TYPE_NONE : source_classify(de.name);
    if (!is_dir && t == SRC_TYPE_NONE)
      continue;
    if (strlen(de.name) >= NAME_MAX_)
      continue;
    entry_t *e = &entries[n++];
    str_copy(e->name, de.name, sizeof(e->name));
    e->is_dir = is_dir;
    e->type = t;
    e->size = ((uint64_t)de.stat.hisize << 32) | de.stat.size;
  }
  fileXioDclose(dd); /* always: no leaked server handles */
  /* Directories first, then by name (simple insertion sort). */
  for (int i = 1; i < n; i++) {
    entry_t t = entries[i];
    int j = i - 1;
    while (j >= 0 && (entries[j].is_dir < t.is_dir ||
                      (entries[j].is_dir == t.is_dir &&
                       strcasecmp(entries[j].name, t.name) > 0))) {
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = t;
  }
  *count = n;
  return 0;
}

static void format_size(uint64_t b, char *out, size_t sz) {
  if (b >= (1ull << 30))
    snprintf(out, sz, "%u.%02u GiB", (unsigned)(b >> 30),
             (unsigned)(((b & ((1ull << 30) - 1)) * 100) >> 30));
  else
    snprintf(out, sz, "%u MiB", (unsigned)(b >> 20));
}

static void parent_dir(char *path) {
  /* "udpfs:/A/B" -> "udpfs:/A"; never above "udpfs:/". */
  size_t n = strlen(path);
  if (n <= 7)
    return;
  char *s = strrchr(path, '/');
  if (s && s > path + 6)
    *s = 0;
  else
    path[7] = 0;
}

static void probe_and_install(const char *path) {
  game_plan_t plan;
  int rc = 0;
  ui_header("Checking", path);
  ui_at(4, " Reading ISO9660 volume descriptor and SYSTEM.CNF...");
  inst_err_t e = game_plan_build(path, &plan, &rc);
  if (e) {
    char msg[512];
    snprintf(msg, sizeof(msg),
             "%s\n\n%s (%s)\nDriver code: %d\n\n%s\n\nNothing was written to the HDD.",
             path, err_text(e), err_name(e), rc,
             e == ERR_SOURCE_INVALID_ISO || e == ERR_SOURCE_SYSTEM_CNF
                 ? "Not a valid PS2 ISO."
                 : "Check the server and the network connection.");
    ui_message("Cannot install", msg);
    return;
  }
  flow_install_game(&plan);
}

void browser_run(void) {
  static char cwd[SOURCE_PATH_MAX] = "udpfs:/";
  int sel = 0;
  for (;;) {
    if (g_app.net != NETWORK_READY) {
      ui_message("Install Games from UDPFS", network_not_ready_text());
      return;
    }
    int n = 0;
    ui_header("Install Games from UDPFS", cwd);
    ui_at(4, " Listing...");
    int r = list_dir(cwd, &n);
    if (r < 0) {
      char msg[SOURCE_PATH_MAX + 64];
      snprintf(msg, sizeof(msg), "Cannot list %s (code %d).", cwd, r);
      ui_message("UDPFS", msg);
      if (strcmp(cwd, "udpfs:/") == 0)
        return;
      parent_dir(cwd);
      continue;
    }
    for (int i = 0; i < n; i++) {
      entry_t *e = &entries[i];
      if (e->is_dir) {
        snprintf(rows[i], UI_ROW_LEN, "[DIR] %.60s/", e->name);
      } else {
        char sz[24];
        format_size(e->size, sz, sizeof(sz));
        snprintf(rows[i], UI_ROW_LEN, "[%s] %-50.50s %10s", source_type_label(e->type),
                 e->name, sz);
      }
    }
    for (int i = 0; i < n; i++) {
      items[i].name = entries[i].name;
      items[i].size = entries[i].size;
      items[i].group = entries[i].is_dir ? -1 : 0; /* folders on top, never filtered */
    }
    char status[96];
    snprintf(status, sizeof(status), "%.50s   %d entries", cwd, n);
    ls.item = sel;
    int pick = listui_pick("Install Games from UDPFS", status, items, rows, n, &ls,
                           "[X] open  [O] up/back", 0, NULL);
    if (pick < 0) {
      if (strcmp(cwd, "udpfs:/") == 0)
        return;
      parent_dir(cwd);
      sel = 0;
      continue;
    }
    sel = pick;
    char path[SOURCE_PATH_MAX];
    int need = snprintf(path, sizeof(path), "%s%s%s", cwd,
                        cwd[strlen(cwd) - 1] == '/' ? "" : "/", entries[pick].name);
    if (need >= (int)sizeof(path)) {
      ui_message("UDPFS", "Path too long.");
      continue;
    }
    if (entries[pick].is_dir) {
      str_copy(cwd, path, sizeof(cwd));
      sel = 0;
    } else {
      /* Open the exact listed path: for ZSO this is udpfsd's virtual
       * "<name>.zso.iso", served as decompressed ISO bytes. */
      probe_and_install(path);
    }
  }
}
