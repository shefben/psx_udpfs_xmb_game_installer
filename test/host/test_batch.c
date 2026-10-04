#include "../../src/batch.h"
#include "../../src/manifest.h"
#include "test.h"

#define MB (1024ull * 1024ull)

static batch_entry_t ent(const char *name, inst_err_t probe, const char *hidden,
                         pair_state_t pair, uint32_t alloc_mb) {
  batch_entry_t e;
  memset(&e, 0, sizeof(e));
  snprintf(e.name, sizeof(e.name), "%s", name);
  snprintf(e.path, sizeof(e.path), "udpfs:/INSTALL/%s", name);
  e.type = SRC_TYPE_ISO;
  e.probe_err = probe;
  snprintf(e.hidden, sizeof(e.hidden), "%s", hidden);
  if (strlen(hidden) > 2)
    snprintf(e.visible, sizeof(e.visible), "PP%s", hidden + 2);
  snprintf(e.boot_id, sizeof(e.boot_id), "SLUS_203.12");
  snprintf(e.title, sizeof(e.title), "%s", name);
  e.bytes = (uint64_t)alloc_mb * MB / 2;
  e.alloc_mb = alloc_mb;
  e.pair = pair;
  return e;
}

TEST(batch_classify_and_default_selection) {
  batch_entry_t e[6];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("bad.iso", ERR_SOURCE_INVALID_ISO, "", PAIR_NONE, 0);
  e[2] = ent("B.zso.iso", ERR_OK, "__.SLUS-20313..B", PAIR_COMPLETE, 1024);
  e[3] = ent("A copy.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[4] = ent("huge.iso", ERR_HDL_PLAN, "__.SLUS-20314..H", PAIR_NONE, 0);
  e[5] = ent("C.iso", ERR_OK, "__.SLUS-20315..C", PAIR_HIDDEN_UNVERIFIED, 512);
  batch_classify(e, 6);
  CHECK_EQ_INT(e[0].status, BATCH_ELIGIBLE);
  CHECK_EQ_INT(e[1].status, BATCH_INVALID);
  CHECK_EQ_INT(e[2].status, BATCH_EXISTS);
  CHECK_EQ_INT(e[3].status, BATCH_DUPLICATE);
  CHECK_EQ_INT(e[3].duplicate_of, 0);
  CHECK_EQ_INT(e[4].status, BATCH_TOO_BIG);
  CHECK_EQ_INT(e[5].status, BATCH_EXISTS); /* unverified data: use Repair */
  CHECK_EQ_INT(batch_count_selected(e, 6), 1);
  CHECK(e[0].selected);
  for (int i = 1; i < 6; i++)
    CHECK(!e[i].selected);
}

TEST(batch_duplicate_of_ineligible_is_still_duplicate_free) {
  /* The first occurrence already exists on the HDD: the second is just
   * EXISTS too, not a duplicate of a selectable entry. */
  batch_entry_t e[2];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_COMPLETE, 4096);
  e[1] = ent("A2.iso", ERR_OK, "__.SLUS-20312..A", PAIR_COMPLETE, 4096);
  batch_classify(e, 2);
  CHECK_EQ_INT(e[0].status, BATCH_EXISTS);
  CHECK_EQ_INT(e[1].status, BATCH_EXISTS);
}

TEST(batch_toggle_only_eligible) {
  batch_entry_t e[2];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_COMPLETE, 1024);
  batch_classify(e, 2);
  CHECK_EQ_INT(batch_toggle(&e[0]), 0);
  CHECK_EQ_INT(batch_toggle(&e[0]), 1);
  CHECK_EQ_INT(batch_toggle(&e[1]), 0);
  CHECK_EQ_INT(e[1].selected, 0);
}

TEST(batch_space_counts_selected_plus_channel) {
  batch_entry_t e[3];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 1024);
  e[2] = ent("C.iso", ERR_OK, "__.SLUS-20314..C", PAIR_NONE, 512);
  batch_classify(e, 3);
  CHECK_EQ_U64(batch_needed_mb(e, 3), 4096 + 1024 + 512 + 3 * 128);
  batch_toggle(&e[1]);
  CHECK_EQ_U64(batch_needed_mb(e, 3), 4096 + 512 + 2 * 128);
}

TEST(batch_rows_and_summary) {
  batch_entry_t e[3];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 1024);
  e[2] = ent("C.iso", ERR_OK, "__.SLUS-20314..C", PAIR_COMPLETE, 512);
  batch_classify(e, 3);
  char row[128];
  batch_format_row(&e[0], row, sizeof(row));
  CHECK(strstr(row, "[x]") == row);
  CHECK(strstr(row, "A.iso") != NULL);
  batch_format_row(&e[2], row, sizeof(row));
  CHECK(strstr(row, "[ ]") == row);
  CHECK(strstr(row, batch_status_label(BATCH_EXISTS)) != NULL);

  e[0].result = BATCH_DONE;
  e[1].result = BATCH_FAILED;
  e[1].err = ERR_SOURCE_READ;
  e[1].stage = "copying game";
  char sum[1024];
  CHECK(batch_summary(e, 3, sum, sizeof(sum)) > 0);
  CHECK(strstr(sum, "1 installed") != NULL);
  CHECK(strstr(sum, "1 failed") != NULL);
  CHECK(strstr(sum, "ERR_SOURCE_READ") != NULL);
  CHECK(strstr(sum, "C.iso") == NULL); /* not selected, not listed */
}

TEST(batch_labels_nonempty) {
  for (int s = BATCH_ELIGIBLE; s <= BATCH_TOO_BIG; s++)
    CHECK(batch_status_label((batch_status_t)s)[0]);
  for (int r = BATCH_PENDING; r <= BATCH_SKIPPED; r++)
    CHECK(batch_result_label((batch_result_t)r)[0]);
}

TEST(batch_from_manifest_duplicate_iso_and_zso) {
  static manifest_t m;
  const char *t =
      "udpfsd-manifest 1 auto=1\n"
      "/DVD/GTA SA.iso\tok\tSLUS_209.46\tGrand Theft Auto: San Andreas\t4697620480\tDVD\t0\t-\t-\n"
      "/DVD/GTA SA.zso.iso\tok\tSLUS_209.46\tGrand Theft Auto: San Andreas\t4697620480\tDVD\t0\t-\t-\n"
      "/DVD/bad.iso\tinvalid:no SYSTEM.CNF\t-\t-\t81920\t-\t0\t-\t-\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  batch_entry_t e[3];
  for (int i = 0; i < 3; i++) {
    batch_entry_from_manifest(&e[i], &m.e[i]);
    if (m.e[i].ok)
      snprintf(e[i].hidden, sizeof(e[i].hidden), "__.SLUS-20946..GRAND_THEFT_AUTO");
  }
  CHECK_STR(e[0].path, "udpfs:/DVD/GTA SA.iso");
  CHECK_STR(e[1].name, "GTA SA.zso.iso");
  CHECK_EQ_INT(e[1].type, SRC_TYPE_ZSO);
  CHECK_STR(e[0].title, "Grand Theft Auto: San Andreas");
  CHECK_EQ_U64(e[0].bytes, 4697620480ull);
  CHECK_EQ_INT(e[2].probe_err, ERR_SOURCE_INVALID_ISO);
  batch_classify(e, 3);
  CHECK_EQ_INT(e[0].status, BATCH_ELIGIBLE);
  CHECK_EQ_INT(e[1].status, BATCH_DUPLICATE);
  CHECK_EQ_INT(e[2].status, BATCH_INVALID);
  CHECK_EQ_INT(batch_count_selected(e, 3), 1);
}
