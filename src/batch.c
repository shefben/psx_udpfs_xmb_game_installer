#include <stdio.h>
#include <string.h>

#include "batch.h"

void batch_classify(batch_entry_t *e, int n) {
  for (int i = 0; i < n; i++) {
    e[i].selected = 0;
    e[i].duplicate_of = -1;
    e[i].result = BATCH_PENDING;
    if (e[i].probe_err == ERR_HDL_PLAN) {
      e[i].status = BATCH_TOO_BIG;
    } else if (e[i].probe_err != ERR_OK) {
      e[i].status = BATCH_INVALID;
    } else if (e[i].pair != PAIR_NONE) {
      e[i].status = BATCH_EXISTS;
    } else {
      e[i].status = BATCH_ELIGIBLE;
      for (int k = 0; k < i; k++) {
        if (e[k].status == BATCH_ELIGIBLE && !strcmp(e[k].hidden, e[i].hidden)) {
          e[i].status = BATCH_DUPLICATE;
          e[i].duplicate_of = k;
          break;
        }
      }
    }
    e[i].selected = e[i].status == BATCH_ELIGIBLE;
  }
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
  }
  return "?";
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
  snprintf(out, outsz, "[%c] %-3s %-34.34s %-11.11s %9s %s", e->selected ? 'x' : ' ',
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
  }
  if (off < 0)
    return 0;
  return (size_t)off < outsz ? (size_t)off : outsz - 1;
}
