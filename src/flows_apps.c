#include <stdio.h>
#include <string.h>

#include "app_install.h"
#include "app_state.h"
#include "browser.h"
#include "flows.h"
#include "hdd_partitions.h"
#include "partname.h"
#include "pfs_channel.h"
#include "space.h"
#include "ui.h"
#include "util.h"

#define APPS_MAX 64

static void mib(uint64_t bytes, char *out, size_t outsz) {
  snprintf(out, outsz, "%lu.%lu MiB", (unsigned long)(bytes >> 20),
           (unsigned long)(((bytes & 0xFFFFF) * 10) >> 20));
}

static void progress(const char *stage, int i, int n, const char *rel) {
  ui_at(10, " %-10s %3d / %-3d", stage, i, n);
  ui_at(11, " %-66.66s", rel);
}

void flow_install_app(const char *elf_path) {
  static app_plan_t only, folder;
  const char *title = "Install App";
  ui_header(title, elf_path);
  ui_at(4, " Reading the app folder...");
  inst_err_t e = app_plan_build(elf_path, 0, &only);
  if (e) {
    char msg[SOURCE_PATH_MAX + 200];
    snprintf(msg, sizeof(msg), "%s\n\n%s (%s)\n\nNothing was written to the HDD.", elf_path,
             err_text(e), err_name(e));
    ui_message("Cannot install", msg);
    return;
  }
  /* A folder app (ELF + its data) unless the ELF lies in a top folder
   * of the device, e.g. udpfs:/APPS/X.ELF or mass0:/X.ELF. */
  const app_plan_t *p = &only;
  const char *rest = strchr(only.dir, ':');
  rest = rest ? rest + 1 : only.dir;
  while (*rest == '/')
    rest++;
  int top = !strchr(rest, '/');
  if (!top && app_plan_build(elf_path, 1, &folder) == ERR_OK && folder.nfiles > 1) {
    char rows[2][UI_ROW_LEN], a[24], b[24];
    mib(only.bytes, a, sizeof(a));
    mib(folder.bytes, b, sizeof(b));
    snprintf(rows[0], UI_ROW_LEN, "The whole folder: %d files, %s%s", folder.nfiles, b,
             folder.skipped ? " (some left out)" : "");
    snprintf(rows[1], UI_ROW_LEN, "Only the ELF: %s", a);
    int c = ui_select(title, only.dir, rows, 2, 0, "[X] select  [O] back", NULL);
    if (c < 0)
      return;
    if (c == 0)
      p = &folder;
  }
  char t[sizeof(p->title)];
  str_copy(t, p->title, sizeof(t));
  if (!ui_edit_text(title, "XMB title of the app:", t, sizeof(t)))
    return;
  app_plan_t *w = (app_plan_t *)p;
  str_copy(w->title, t[0] ? t : "App", sizeof(w->title));

  uint32_t size_mb = 0;
  const char *size_str = app_size_str((p->bytes + (1u << 20) - 1) >> 20, &size_mb);
  char sz[24], msg[900];
  mib(p->bytes, sz, sizeof(sz));
  snprintf(msg, sizeof(msg),
           "Title       %s\n"
           "ELF         %s\n"
           "Files       %d (%s)%s\n"
           "Partition   PP.APPS-#####, %s PFS\n\n"
           "The app gets its own XMB channel. Its launcher starts the ELF\n"
           "from the HDD; apps that look for files next to themselves\n"
           "may not find them.\n\nInstall?",
           p->title, p->files[p->boot].rel, p->nfiles, sz,
           p->skipped ? ", some files left out" : "", size_str ? size_str : "too big");
  if (!size_str) {
    ui_message(title, "The app is bigger than 2 GiB; it cannot be installed as a channel.");
    return;
  }
  if (!ui_confirm(title, msg))
    return;

  ui_header(title, p->title);
  ui_at(4, " Creating the app channel...");
  app_report_t rep;
  app_install(p, progress, &rep);
  if (rep.err) {
    snprintf(msg, sizeof(msg),
             "%s (%s)\nStep: %s\nDriver code: %d\n\nThe partition was removed again;\n"
             "nothing else was changed.",
             err_text(rep.err), err_name(rep.err), rep.step ? rep.step : "-", rep.rc);
    ui_message("App not installed", msg);
    return;
  }
  snprintf(msg, sizeof(msg),
           "%s\n\nis now an XMB channel (%s). Restart the DESR to see it.",
           p->title, rep.partition);
  ui_message("App installed", msg);
}

static void installed_apps(void) {
  static hdd_part_t parts[256];
  static char names[APPS_MAX][APA_NAME_MAX + 1];
  static char rows[APPS_MAX][UI_ROW_LEN];
  int sel = 0;
  for (;;) {
    ui_header("Installed Apps", NULL);
    ui_at(4, " Reading the HDD...");
    int np = hdd_list(parts, 256), n = 0;
    for (int i = 0; i < np && n < APPS_MAX; i++) {
      if (!partition_is_app(parts[i].name) || parts[i].type != APA_TYPE_PFS_ID)
        continue;
      str_copy(names[n], parts[i].name, sizeof(names[n]));
      char t[64];
      if (channel_get_title(names[n], t, sizeof(t)) < 0)
        str_copy(t, "?", sizeof(t));
      snprintf(rows[n], UI_ROW_LEN, "%-32.32s %-30.30s %5lu MiB", names[n], t,
               (unsigned long)(parts[i].size_sectors / 2048));
      n++;
    }
    if (!n) {
      ui_message("Installed Apps", "No app channels on the HDD.");
      return;
    }
    char status[32];
    snprintf(status, sizeof(status), "%d app%s", n, n == 1 ? "" : "s");
    int c = ui_select("Installed Apps", status, rows, n, sel, "[X] delete  [O] back", NULL);
    if (c < 0)
      return;
    sel = c;
    char txt[300];
    snprintf(txt, sizeof(txt),
             "This PERMANENTLY removes the app channel\n\n  %s\n\nand every file in it.",
             names[c]);
    if (!ui_confirm_destructive("Delete app", txt))
      continue;
    pfs_umount(PFS_WORK);
    int rc = 0;
    inst_err_t e = hdd_remove_exact(names[c], &rc);
    if (e) {
      snprintf(txt, sizeof(txt), "Could not remove %s\n%s (code %d).", names[c], err_text(e), rc);
      ui_message("Delete failed", txt);
    }
  }
}

void flow_apps(void) {
  static char rows[3][UI_ROW_LEN] = {
      "Install App from UDPFS (server folder APPS)",
      "Install App from USB (folder APPS)",
      "Installed Apps",
  };
  int sel = 0;
  for (;;) {
    int c = ui_select("Apps", "Homebrew .ELF files as XMB channels", rows, 3, sel,
                      "[X] select  [O] back", NULL);
    if (c < 0)
      return;
    sel = c;
    if (c == 2)
      installed_apps();
    else
      browser_run_apps(c == 1);
  }
}
