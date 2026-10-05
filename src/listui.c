#include <stdio.h>
#include <string.h>

#include "listui.h"
#include "util.h"

static int view_idx[LISTUI_MAX];
static char view_rows[LISTUI_MAX][UI_ROW_LEN];

int listui_pick(const char *title, const char *status, const lv_item_t *items,
                char rows[][UI_ROW_LEN], int n, listui_state_t *st, const char *footer,
                int key_mask, int *key_out) {
  if (n > LISTUI_MAX)
    n = LISTUI_MAX;
  if (key_out)
    *key_out = 0;
  for (;;) {
    int m = lv_build(items, n, st->sort, st->filter, view_idx);
    for (int i = 0; i < m; i++)
      memcpy(view_rows[i], rows[view_idx[i]], UI_ROW_LEN);
    char full[160];
    snprintf(full, sizeof(full), "%.80s  [%s%s%s]", status, lv_sort_label(st->sort),
             st->filter[0] ? ", search: " : "", st->filter);
    char foot[UI_ROW_LEN];
    snprintf(foot, sizeof(foot), "%s  [L2] sort  [R2] search",
             footer ? footer : "[X] select  [O] back");
    int key = 0;
    int c = ui_select_ex(title, full, view_rows, m, lv_find(view_idx, m, st->item), foot,
                         key_mask | UI_L2 | UI_R2, &key);
    if (c < 0)
      return -1;
    if (m > 0)
      st->item = view_idx[c];
    if (key & UI_L2) {
      st->sort = lv_next_sort(st->sort);
      continue;
    }
    if (key & UI_R2) {
      char f[sizeof(st->filter)];
      str_copy(f, st->filter, sizeof(f));
      if (ui_edit_text("Search", "Show only names containing (empty = all):", f,
                       sizeof(f) - 1))
        str_copy(st->filter, f, sizeof(st->filter));
      continue;
    }
    if (m == 0)
      continue; /* nothing to choose: only sort/search/back */
    if (key_out)
      *key_out = key;
    return st->item;
  }
}
