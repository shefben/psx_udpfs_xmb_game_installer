#ifndef PSXI_LISTVIEW_H
#define PSXI_LISTVIEW_H

#include <stdint.h>

/* Sorted, filtered view of a list (game browsers, installed games).
 * Pure: builds an index array into the caller's items. */

typedef struct {
  const char *name; /* sort key and search text */
  uint64_t size;
  int group; /* lower groups first, whatever the order (e.g. folders = -1);
                group < 0 is always shown, even when filtered out */
} lv_item_t;

typedef enum { LV_SORT_NAME = 0, LV_SORT_NAME_DESC, LV_SORT_SIZE_DESC, LV_SORT__COUNT } lv_sort_t;

/* Fill out_idx with the visible items in display order; returns how many. */
int lv_build(const lv_item_t *items, int n, lv_sort_t sort, const char *filter, int *out_idx);

/* Row of item `item` in the view, or 0 if it is not shown. */
int lv_find(const int *idx, int n, int item);

const char *lv_sort_label(lv_sort_t s);
lv_sort_t lv_next_sort(lv_sort_t s);

#endif
