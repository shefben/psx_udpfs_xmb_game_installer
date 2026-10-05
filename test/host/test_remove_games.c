#include "../../src/remove_games.h"
#include "../../src/source.h"
#include "test.h"

static remove_entry_t rent(const char *hidden, pair_state_t st) {
  remove_entry_t e;
  char visible[APA_NAME_MAX + 1];
  snprintf(visible, sizeof(visible), "PP%s", hidden + 2);
  remove_entry_init(&e, visible, hidden, st);
  return e;
}

TEST(remove_entries_start_unselected_and_toggle) {
  remove_entry_t e[2] = {rent("__.SLUS-20312..GTA", PAIR_COMPLETE),
                         rent("__.SLUS-20066..HALF LIFE", PAIR_ORPHAN_CHANNEL)};
  CHECK_EQ_INT(remove_count_selected(e, 2), 0);
  CHECK_EQ_INT(remove_toggle(&e[1]), 1);
  CHECK_EQ_INT(remove_count_selected(e, 2), 1);
  CHECK_EQ_INT(remove_toggle(&e[1]), 0);
  CHECK_EQ_INT(remove_count_selected(e, 2), 0);
}

TEST(remove_toggle_all_selects_all_then_none) {
  remove_entry_t e[3] = {rent("__.SLUS-20312..A", PAIR_COMPLETE),
                         rent("__.SLUS-20313..B", PAIR_COMPLETE),
                         rent("__.SLUS-20314..C", PAIR_HIDDEN_UNVERIFIED)};
  remove_toggle(&e[0]);
  CHECK_EQ_INT(remove_toggle_all(e, 3), 3); /* some selected: select all */
  CHECK_EQ_INT(remove_toggle_all(e, 3), 0); /* all selected: select none */
}

TEST(remove_row_shows_mark_name_and_state) {
  remove_entry_t e = rent("__.SLUS-20312..GTA SAN ANDREAS", PAIR_COMPLETE);
  char row[96];
  remove_format_row(&e, row, sizeof(row));
  CHECK(strncmp(row, "[ ] SLUS-20312..GTA SAN ANDREAS", 31) == 0);
  CHECK(strstr(row, pair_state_label(PAIR_COMPLETE)) != NULL);
  CHECK(strlen(row) <= 70);
  remove_toggle(&e);
  remove_format_row(&e, row, sizeof(row));
  CHECK(strncmp(row, "[x] ", 4) == 0);
}

TEST(remove_summary_counts_and_lists_failures) {
  remove_entry_t e[3] = {rent("__.SLUS-20312..A", PAIR_COMPLETE),
                         rent("__.SLUS-20313..B", PAIR_COMPLETE),
                         rent("__.SLUS-20314..C", PAIR_COMPLETE)};
  e[0].selected = e[1].selected = 1; /* C not selected: not listed */
  e[0].result = REMOVE_DONE;
  e[1].result = REMOVE_FAILED;
  e[1].failed = e[1].hidden;
  e[1].rc = -5;
  char sum[512];
  remove_summary(e, 3, sum, sizeof(sum));
  CHECK(strncmp(sum, "1 removed, 1 failed", 19) == 0);
  CHECK(strstr(sum, "removed   SLUS-20312..A") != NULL);
  CHECK(strstr(sum, "FAILED    SLUS-20313..B") != NULL);
  CHECK(strstr(sum, "__.SLUS-20313..B still exists (code -5)") != NULL);
  CHECK(strstr(sum, "SLUS-20314..C") == NULL);
}

TEST(backup_path_is_fat_safe_and_opl_style) {
  char p[SOURCE_PATH_MAX];
  CHECK_EQ_INT(backup_path(p, sizeof(p), "mass0:/", 1, "SLUS_203.12", "Final Fantasy X", 0, ".iso"), 0);
  CHECK_STR(p, "mass0:/DVD/SLUS_203.12.Final Fantasy X.iso");
  backup_path(p, sizeof(p), "mass0:/", 0, "SCUS_944.26", "A/B: C*D?\"<>|", 2, ".iso");
  CHECK_STR(p, "mass0:/CD/SCUS_944.26.A_B_ C_D_____ (2).iso");
  backup_path(p, sizeof(p), "mass0:/", -1, "SLUS_005.94", "  ", 0, ".VCD");
  CHECK_STR(p, "mass0:/POPS/SLUS_005.94.VCD"); /* PS1: POPStarter folder, no title */
}

TEST(remove_summary_truncates_safely) {
  remove_entry_t e[4];
  for (int i = 0; i < 4; i++) {
    e[i] = rent("__.SLUS-20312..A VERY LONG GAME TITLE", PAIR_COMPLETE);
    e[i].selected = 1;
    e[i].result = REMOVE_DONE;
  }
  char sum[40];
  size_t n = remove_summary(e, 4, sum, sizeof(sum));
  CHECK(n < sizeof(sum));
  CHECK_EQ_INT((int)strlen(sum), (int)n);
}
