#include "test.h"
#include "game_pair.h"

TEST(repair_batch_never_rebuilds_unverified_or_orphaned_game_data) {
  CHECK_EQ_INT(pair_repair_actions(PAIR_HIDDEN_UNVERIFIED, 0, 1), 0);
  CHECK_EQ_INT(pair_repair_actions(PAIR_HIDDEN_INVALID_WITH_CHANNEL, 0, 1), 0);
  CHECK_EQ_INT(pair_repair_actions(PAIR_ORPHAN_CHANNEL, 0, 1), 0);
  CHECK_EQ_INT(pair_repair_actions(PAIR_NONE, 0, 0), 0);
}
TEST(repair_batch_selects_safe_action_for_each_layout) {
  CHECK_EQ_INT(pair_repair_actions(PAIR_HIDDEN_ONLY, 0, 0), REPAIR_CHANNEL);
  CHECK_EQ_INT(pair_repair_actions(PAIR_CHANNEL_BROKEN, 0, 1), REPAIR_CHANNEL);
  CHECK_EQ_INT(pair_repair_actions(PAIR_COMPLETE, 1, 0), REPAIR_ADD_COVER);
  CHECK_EQ_INT(pair_repair_actions(PAIR_COMPLETE, 0, 1), REPAIR_CHANNEL | REPAIR_IMAGE);
  CHECK_EQ_INT(pair_repair_actions(PAIR_PS1, 0, 1), REPAIR_PS1_HEADER | REPAIR_IMAGE);
}
