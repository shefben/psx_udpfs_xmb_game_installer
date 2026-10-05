#include <stdio.h>
#include <string.h>

#include "remove_games.h"
#include "util.h"

void remove_entry_init(remove_entry_t *e, const char *visible, const char *hidden,
                       pair_state_t state) {
  memset(e, 0, sizeof(*e));
  str_copy(e->visible, visible, sizeof(e->visible));
  str_copy(e->hidden, hidden, sizeof(e->hidden));
  e->state = state;
}

int remove_toggle(remove_entry_t *e) { return e->selected = !e->selected; }

int remove_toggle_all(remove_entry_t *e, int n) {
  int on = remove_count_selected(e, n) != n;
  for (int i = 0; i < n; i++)
    e[i].selected = on;
  return on ? n : 0;
}

int remove_count_selected(const remove_entry_t *e, int n) {
  int c = 0;
  for (int i = 0; i < n; i++)
    c += e[i].selected != 0;
  return c;
}

/* "SLUS-20312..TITLE": the partition name without its PP./__. prefix. */
static const char *display_name(const remove_entry_t *e) {
  const char *n = e->visible[0] ? e->visible : e->hidden;
  return strlen(n) > 3 ? n + 3 : n;
}

void remove_format_row(const remove_entry_t *e, char *out, size_t outsz) {
  snprintf(out, outsz, "[%c] %-34.34s %.31s", e->selected ? 'x' : ' ', display_name(e),
           pair_state_label(e->state));
}

int backup_path(char *out, size_t outsz, const char *root, int dvd, const char *boot_id,
                const char *title, int n, const char *ext) {
  char t[64];
  size_t k = 0;
  for (const char *p = title; *p && k < sizeof(t) - 1; p++)
    t[k++] = (strchr("\\/:*?\"<>|", *p) || (unsigned char)*p < 0x20) ? '_' : *p;
  t[k] = 0;
  while (k > 0 && (t[k - 1] == ' ' || t[k - 1] == '.'))
    t[--k] = 0;
  const char *s = t;
  while (*s == ' ')
    s++;
  char num[16] = "";
  if (n > 0)
    snprintf(num, sizeof(num), " (%d)", n);
  int r = snprintf(out, outsz, "%s%s/%s%s%s%s%s", root, dvd < 0 ? "POPS" : dvd ? "DVD" : "CD",
                   boot_id, *s ? "." : "", s, num, ext);
  return r < 0 || (size_t)r >= outsz ? -1 : 0;
}

size_t remove_summary(const remove_entry_t *e, int n, char *out, size_t outsz) {
  int done = 0, failed = 0;
  for (int i = 0; i < n; i++) {
    if (!e[i].selected)
      continue;
    done += e[i].result == REMOVE_DONE;
    failed += e[i].result == REMOVE_FAILED;
  }
  int off = snprintf(out, outsz, "%d removed, %d failed\n\n", done, failed);
  for (int i = 0; i < n && off > 0 && (size_t)off < outsz; i++) {
    if (!e[i].selected)
      continue;
    off += snprintf(out + off, outsz - off, "%-9s %.50s\n",
                    e[i].result == REMOVE_DONE     ? "removed"
                    : e[i].result == REMOVE_FAILED ? "FAILED"
                                                   : "skipped",
                    display_name(&e[i]));
    if (e[i].result == REMOVE_FAILED && (size_t)off < outsz)
      off += snprintf(out + off, outsz - off, "          %s still exists (code %d)\n",
                      e[i].failed ? e[i].failed : "?", e[i].rc);
  }
  if (off < 0)
    return 0;
  return (size_t)off < outsz ? (size_t)off : outsz - 1;
}
