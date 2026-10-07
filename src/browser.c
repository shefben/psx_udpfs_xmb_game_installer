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
  int is_app; /* a homebrew .ELF: installed as an XMB app channel */
  source_type_t type;
  uint64_t size;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static char rows[MAX_ENTRIES][UI_ROW_LEN];
static lv_item_t items[MAX_ENTRIES];

static int list_dir(const char *dir, int server, int *count) {
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
    /* udpfsd serves a ZSO as its decompressed "<name>.zso.iso"; a raw
     * .zso is only read directly from local devices (USB). */
    if (server && t == SRC_TYPE_ZSO_FILE)
      t = SRC_TYPE_NONE;
    int is_app = !is_dir && t == SRC_TYPE_NONE && str_ends_with_ci(de.name, ".elf");
    if (!is_dir && t == SRC_TYPE_NONE && !is_app)
      continue;
    if (strlen(de.name) >= NAME_MAX_)
      continue;
    entry_t *e = &entries[n++];
    str_copy(e->name, de.name, sizeof(e->name));
    e->is_dir = is_dir;
    e->is_app = is_app;
    e->type = t;
    e->size = ((uint64_t)de.stat.hisize << 32) | de.stat.size;
  }
  fileXioDclose(dd); /* always: no leaked server handles */
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

/* "dev:/A/B" -> "dev:/A"; never above the root "dev:/". */
static void parent_dir(char *path, size_t root_len) {
  size_t n = strlen(path);
  if (n <= root_len)
    return;
  char *s = strrchr(path, '/');
  if (s && s >= path + root_len)
    *s = 0;
  else
    path[root_len] = 0;
}

static void probe_and_install(const char *path, int server) {
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
                 ? "Not a valid PS2 ISO (or an unsupported ZSO)."
             : server ? "Check the server and the network connection."
                      : "Check the USB drive (FAT32 or exFAT).");
    ui_message("Cannot install", msg);
    return;
  }
  flow_install_game(&plan);
}

static void browse(const char *title, const char *root, char *cwd, listui_state_t *ls) {
  int server = !strncmp(root, "udpfs:", 6);
  size_t root_len = strlen(root);
  int sel = 0;
  for (;;) {
    if (server && g_app.net != NETWORK_READY) {
      ui_message(title, network_not_ready_text());
      return;
    }
    int n = 0;
    ui_header(title, cwd);
    ui_at(4, " Listing...");
    int r = list_dir(cwd, server, &n);
    if (r < 0) {
      char msg[SOURCE_PATH_MAX + 200];
      if (!server && strcmp(cwd, root) == 0)
        snprintf(msg, sizeof(msg),
                 "No USB drive found (code %d).\n\n"
                 "Plug a FAT32 or exFAT formatted USB drive into the DESR,\n"
                 "wait a few seconds and try again. Games larger than 4 GiB\n"
                 "need exFAT (FAT32 cannot hold them).",
                 r);
      else
        snprintf(msg, sizeof(msg), "Cannot list %s (code %d).", cwd, r);
      ui_message(title, msg);
      if (strcmp(cwd, root) == 0)
        return;
      parent_dir(cwd, root_len);
      continue;
    }
    for (int i = 0; i < n; i++) {
      entry_t *e = &entries[i];
      if (e->is_dir) {
        snprintf(rows[i], UI_ROW_LEN, "[DIR] %.60s/", e->name);
      } else {
        char sz[24], shown[NAME_MAX_];
        format_size(e->size, sz, sizeof(sz));
        snprintf(rows[i], UI_ROW_LEN, "[%.3s] %-50.50s %10.10s",
                 e->is_app ? "APP" : source_type_label(e->type),
                 source_display_name(e->name, shown, sizeof(shown)), sz);
      }
      items[i].name = e->name;
      items[i].size = e->size;
      items[i].group = e->is_dir ? -1 : 0; /* folders on top, never filtered */
    }
    char status[96];
    snprintf(status, sizeof(status), "%.50s   %d entries", cwd, n);
    ls->item = sel;
    int pick = listui_pick(title, status, items, rows, n, ls, "[X] open  [O] up/back", 0, NULL);
    if (pick < 0) {
      if (strcmp(cwd, root) == 0)
        return;
      parent_dir(cwd, root_len);
      sel = 0;
      continue;
    }
    sel = pick;
    char path[SOURCE_PATH_MAX];
    int need = snprintf(path, sizeof(path), "%s%s%s", cwd,
                        cwd[strlen(cwd) - 1] == '/' ? "" : "/", entries[pick].name);
    if (need >= (int)sizeof(path)) {
      ui_message(title, "Path too long.");
      continue;
    }
    if (entries[pick].is_dir) {
      str_copy(cwd, path, SOURCE_PATH_MAX);
      sel = 0;
    } else {
      /* Server ZSO: the exact listed path is udpfsd's virtual
       * "<name>.zso.iso", served as decompressed ISO bytes. USB ZSO: the
       * raw file, decompressed on the PS2 (source_zso). */
      if (entries[pick].is_app)
        flow_install_app(path);
      else if (entries[pick].type == SRC_TYPE_VCD)
        flow_install_ps1(path);
      else
        probe_and_install(path, server);
    }
  }
}

void browser_run(void) {
  static char cwd[SOURCE_PATH_MAX] = "udpfs:/";
  static listui_state_t ls; /* order and search kept while browsing */
  browse("Install Games from UDPFS", "udpfs:/", cwd, &ls);
}

/* Start in <root>APPS when that folder exists, else at the root. */
static void apps_start(char *cwd, const char *root) {
  char apps[SOURCE_PATH_MAX];
  snprintf(apps, sizeof(apps), "%sAPPS", root);
  int dd = fileXioDopen(apps);
  if (dd >= 0) {
    fileXioDclose(dd);
    str_copy(cwd, apps, SOURCE_PATH_MAX);
  } else {
    str_copy(cwd, root, SOURCE_PATH_MAX);
  }
}

void browser_run_apps(int usb) {
  static char cwd[SOURCE_PATH_MAX];
  static listui_state_t ls;
  const char *root = usb ? USB_ROOT : "udpfs:/";
  if (usb && !g_app.iop.usb_ok) {
    ui_message("Install App from USB",
               "The USB drivers failed to load (see Diagnostics), so USB\n"
               "drives cannot be read.");
    return;
  }
  if (!usb && g_app.net != NETWORK_READY) {
    ui_message("Install App from UDPFS", network_not_ready_text());
    return;
  }
  apps_start(cwd, root);
  browse(usb ? "Install App from USB" : "Install App from UDPFS", root, cwd, &ls);
}

void browser_run_usb(void) {
  static char cwd[SOURCE_PATH_MAX] = USB_ROOT;
  static listui_state_t ls;
  if (!g_app.iop.usb_ok) {
    ui_message("Install Games from USB",
               "The USB drivers failed to load (see Diagnostics), so USB\n"
               "drives cannot be read.");
    return;
  }
  browse("Install Games from USB", USB_ROOT, cwd, &ls);
}
