#include <stdio.h>
#include <string.h>

#include "batch.h"
#include "util.h"

void batch_entry_from_manifest(batch_entry_t *e, const manifest_entry_t *m) {
  memset(e, 0, sizeof(*e));
  /* manifest_parse() rejects paths too long for the "udpfs:" prefix */
  snprintf(e->path, sizeof(e->path), "udpfs:%.*s", (int)sizeof(e->path) - 7, m->path);
  const char *slash = strrchr(m->path, '/');
  str_copy(e->name, slash ? slash + 1 : m->path, sizeof(e->name));
  e->type = source_classify(e->name);
  e->bytes = m->bytes;
  e->probe_err = m->ok ? ERR_OK : ERR_SOURCE_INVALID_ISO;
  str_copy(e->boot_id, m->id, sizeof(e->boot_id));
  str_copy(e->title, m->title, sizeof(e->title));
}

void batch_classify(batch_entry_t *e, int n) {
  for (int i = 0; i < n; i++) {
    e[i].selected = 0;
    e[i].duplicate_of = -1;
    e[i].result = BATCH_PENDING;
    if (e[i].probe_err == ERR_HDL_PLAN) {
      e[i].status = BATCH_TOO_BIG;
    } else if (e[i].probe_err != ERR_OK) {
      e[i].status = BATCH_INVALID;
    } else if (e[i].pair != PAIR_NONE || e[i].id_on_hdd) {
      e[i].status = BATCH_EXISTS;
    } else {
      e[i].status = BATCH_ELIGIBLE;
      /* Same game ID = same game, whatever its title or file name. */
      for (int k = 0; k < i; k++) {
        if (e[k].status == BATCH_ELIGIBLE && !strcmp(e[k].boot_id, e[i].boot_id)) {
          e[i].status = BATCH_DUPLICATE;
          e[i].duplicate_of = k;
          break;
        }
      }
    }
    e[i].selected = e[i].status == BATCH_ELIGIBLE;
  }
}

void batch_mark_on_hdd(batch_entry_t *e, int n, const char *const *names, int nnames) {
  for (int i = 0; i < n; i++) {
    char pid[PART_ID_LEN + 1];
    e[i].id_on_hdd = 0;
    if (!e[i].boot_id[0] || boot_id_to_part_id(e[i].boot_id, pid))
      continue;
    for (int k = 0; k < nnames && !e[i].id_on_hdd; k++) {
      const char *p = names[k];
      e[i].id_on_hdd = (!strncmp(p, "__.", 3) || !strncmp(p, "PP.", 3)) &&
                       !strncmp(p + 3, pid, PART_ID_LEN) &&
                       !strncmp(p + 3 + PART_ID_LEN, "..", 2);
    }
  }
}

int auto_should_wait(int loaded, const manifest_t *m) {
  return !loaded || !m || m->scanning;
}

auto_step_t auto_installer_step(int exists, int mounted) {
  if (!exists)
    return AUTO_CREATE_INSTALLER;
  return mounted ? AUTO_INSTALLER_OK : AUTO_STOP;
}

int batch_toggle(batch_entry_t *e) {
  if (e->status != BATCH_ELIGIBLE)
    return e->selected = 0;
  return e->selected = !e->selected;
}

int batch_count_selected(const batch_entry_t *e, int n) {
  int c = 0;
  for (int i = 0; i < n; i++)
    c += e[i].selected != 0;
  return c;
}

uint64_t batch_needed_mb(const batch_entry_t *e, int n) {
  uint64_t mb = 0;
  for (int i = 0; i < n; i++)
    if (e[i].selected)
      mb += (uint64_t)e[i].alloc_mb + 128; /* + the 128 MiB XMB channel */
  return mb;
}

const char *batch_status_label(batch_status_t s) {
  switch (s) {
  case BATCH_ELIGIBLE:
    return "new";
  case BATCH_INVALID:
    return "not a PS2 image";
  case BATCH_EXISTS:
    return "already on HDD";
  case BATCH_DUPLICATE:
    return "duplicate";
  case BATCH_TOO_BIG:
    return "too big for APA";
  case BATCH_NO_SPACE:
    return "no space";
  }
  return "?";
}

int batch_auto_select(batch_entry_t *e, int n, uint64_t free_mb) {
  uint64_t used = 0;
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (e[i].status == BATCH_NO_SPACE)
      e[i].status = BATCH_ELIGIBLE;
    e[i].selected = 0;
    if (e[i].status != BATCH_ELIGIBLE)
      continue;
    uint64_t need = (uint64_t)e[i].alloc_mb + 128;
    if (used + need > free_mb) {
      e[i].status = BATCH_NO_SPACE;
      continue;
    }
    used += need;
    e[i].selected = 1;
    count++;
  }
  return count;
}

const char *batch_result_label(batch_result_t r) {
  switch (r) {
  case BATCH_PENDING:
    return "pending";
  case BATCH_DONE:
    return "installed";
  case BATCH_DATA_ONLY:
    return "data only, channel pending";
  case BATCH_FAILED:
    return "FAILED";
  case BATCH_SKIPPED:
    return "skipped";
  }
  return "?";
}

void batch_format_row(const batch_entry_t *e, char *out, size_t outsz) {
  char size[16] = "";
  if (e->probe_err == ERR_OK || e->probe_err == ERR_HDL_PLAN)
    snprintf(size, sizeof(size), "%u MiB", (unsigned)(e->bytes >> 20));
  /* <= 70 chars: fits a list row of the TV-safe screen */
  snprintf(out, outsz, "[%c] %-3s %-24.24s %-11.11s %9s %s", e->selected ? 'x' : ' ',
           source_type_label(e->type), e->name, e->boot_id[0] ? e->boot_id : "-", size,
           batch_status_label(e->status));
}

size_t batch_summary(const batch_entry_t *e, int n, char *out, size_t outsz) {
  int done = 0, data = 0, failed = 0, skipped = 0;
  for (int i = 0; i < n; i++) {
    if (!e[i].selected)
      continue;
    done += e[i].result == BATCH_DONE;
    data += e[i].result == BATCH_DATA_ONLY;
    failed += e[i].result == BATCH_FAILED;
    skipped += e[i].result == BATCH_SKIPPED || e[i].result == BATCH_PENDING;
  }
  int off = snprintf(out, outsz,
                     "%d installed, %d data only (channel pending), %d failed, %d skipped\n\n",
                     done, data, failed, skipped);
  for (int i = 0; i < n && off > 0 && (size_t)off < outsz; i++) {
    if (!e[i].selected)
      continue;
    off += snprintf(out + off, outsz - off, "%-9s %.40s\n", batch_result_label(e[i].result),
                    e[i].name);
    if (e[i].result == BATCH_FAILED && (size_t)off < outsz)
      off += snprintf(out + off, outsz - off, "          %s at %s\n", err_name(e[i].err),
                      e[i].stage ? e[i].stage : "-");
    if (e[i].result == BATCH_DONE && e[i].opl_cfg && !strcmp(e[i].opl_cfg, "failed") &&
        (size_t)off < outsz)
      off += snprintf(out + off, outsz - off, "          OPL cfg not copied\n");
  }
  if (off < 0)
    return 0;
  return (size_t)off < outsz ? (size_t)off : outsz - 1;
}
