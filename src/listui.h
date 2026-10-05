#ifndef PSXI_LISTUI_H
#define PSXI_LISTUI_H

#include "listview.h"
#include "ui.h"

/* A selection list with sorting (L2 cycles the order) and search (R2
 * edits a filter text; an empty text shows everything). */

#define LISTUI_MAX 256

typedef struct {
  lv_sort_t sort;
  char filter[32];
  int item; /* selected item (index into the caller's items) */
} listui_state_t;

/* `rows[i]` is the display row of items[i]. Returns the chosen item
 * index (X, or a key of key_mask with *key_out set), or -1 on O/back.
 * The status line gets the order and filter appended. */
int listui_pick(const char *title, const char *status, const lv_item_t *items,
                char rows[][UI_ROW_LEN], int n, listui_state_t *st, const char *footer,
                int key_mask, int *key_out);

#endif
