#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "listview.h"

static int contains_ci(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  if (n == 0)
    return 1;
  for (; *hay; hay++)
    if (strncasecmp(hay, needle, n) == 0)
      return 1;
  return 0;
}

static int before(const lv_item_t *a, const lv_item_t *b, lv_sort_t sort) {
  if (a->group != b->group)
    return a->group < b->group;
  int c = strcasecmp(a->name, b->name);
  switch (sort) {
  case LV_SORT_NAME_DESC:
    return c > 0;
  case LV_SORT_SIZE_DESC:
    return a->size != b->size ? a->size > b->size : c < 0;
  default:
    return c < 0;
  }
}

int lv_build(const lv_item_t *items, int n, lv_sort_t sort, const char *filter, int *out_idx) {
  int m = 0;
  for (int i = 0; i < n; i++)
    if (items[i].group < 0 || !filter || contains_ci(items[i].name, filter))
      out_idx[m++] = i;
  /* Insertion sort: stable, lists are at most a few hundred entries. */
  for (int i = 1; i < m; i++) {
    int t = out_idx[i], j = i - 1;
    while (j >= 0 && before(&items[t], &items[out_idx[j]], sort)) {
      out_idx[j + 1] = out_idx[j];
      j--;
    }
    out_idx[j + 1] = t;
  }
  return m;
}

int lv_find(const int *idx, int n, int item) {
  for (int i = 0; i < n; i++)
    if (idx[i] == item)
      return i;
  return 0;
}

const char *lv_sort_label(lv_sort_t s) {
  switch (s) {
  case LV_SORT_NAME_DESC:
    return "name Z-A";
  case LV_SORT_SIZE_DESC:
    return "size";
  default:
    return "name A-Z";
  }
}

lv_sort_t lv_next_sort(lv_sort_t s) { return (lv_sort_t)((s + 1) % LV_SORT__COUNT); }
