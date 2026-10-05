#include "../../src/listview.h"
#include "test.h"

static const lv_item_t ITEMS[] = {
    {"Gran Turismo 4", 4000, 0}, {"ape escape 3", 3000, 0}, {"Okami", 4300, 0},
    {"DVD", 0, -1},              {"Burnout 3", 2000, 1},
};
#define N ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

TEST(lv_sort_by_name_groups_first_case_insensitive) {
  int idx[N];
  CHECK_EQ_INT(lv_build(ITEMS, N, LV_SORT_NAME, "", idx), N);
  /* group -1 (folders) first, then group 0, then 1; names A-Z inside */
  CHECK_STR(ITEMS[idx[0]].name, "DVD");
  CHECK_STR(ITEMS[idx[1]].name, "ape escape 3");
  CHECK_STR(ITEMS[idx[2]].name, "Gran Turismo 4");
  CHECK_STR(ITEMS[idx[3]].name, "Okami");
  CHECK_STR(ITEMS[idx[4]].name, "Burnout 3");
}

TEST(lv_sort_desc_and_size) {
  int idx[N];
  lv_build(ITEMS, N, LV_SORT_NAME_DESC, "", idx);
  CHECK_STR(ITEMS[idx[0]].name, "DVD");
  CHECK_STR(ITEMS[idx[1]].name, "Okami");
  lv_build(ITEMS, N, LV_SORT_SIZE_DESC, "", idx);
  CHECK_STR(ITEMS[idx[1]].name, "Okami");
  CHECK_STR(ITEMS[idx[2]].name, "Gran Turismo 4");
  /* groups are kept: the folder stays on top whatever the order */
  CHECK_STR(ITEMS[idx[0]].name, "DVD");
}

TEST(lv_filter_substring_case_insensitive_keeps_folders) {
  int idx[N];
  int n = lv_build(ITEMS, N, LV_SORT_NAME, "TUR", idx);
  CHECK_EQ_INT(n, 2);
  CHECK_STR(ITEMS[idx[0]].name, "DVD"); /* folders always shown */
  CHECK_STR(ITEMS[idx[1]].name, "Gran Turismo 4");
  CHECK_EQ_INT(lv_build(ITEMS, N, LV_SORT_NAME, "zzz", idx), 1);
  CHECK_EQ_INT(lv_build(ITEMS, N, LV_SORT_NAME, NULL, idx), N);
}

TEST(lv_find_keeps_selection_on_same_item) {
  int idx[N];
  int n = lv_build(ITEMS, N, LV_SORT_NAME, "", idx);
  CHECK_EQ_INT(lv_find(idx, n, 2), 3); /* "Okami" is row 3 */
  CHECK_EQ_INT(lv_find(idx, n, 99), 0);
}

TEST(lv_labels) {
  CHECK_STR(lv_sort_label(LV_SORT_NAME), "name A-Z");
  CHECK_STR(lv_sort_label(LV_SORT_NAME_DESC), "name Z-A");
  CHECK_STR(lv_sort_label(LV_SORT_SIZE_DESC), "size");
  CHECK_EQ_INT(lv_next_sort(LV_SORT_SIZE_DESC), LV_SORT_NAME);
}
