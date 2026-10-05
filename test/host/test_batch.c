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
  /* boot ID from the hidden name: "__.SLUS-20312..A" -> "SLUS_203.12" */
  if (strlen(hidden) >= 13)
    snprintf(e.boot_id, sizeof(e.boot_id), "%.4s_%.3s.%.2s", hidden + 3, hidden + 8,
             hidden + 11);
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
  for (int s = BATCH_ELIGIBLE; s <= BATCH_NO_SPACE; s++)
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

TEST(auto_start_only_when_idle_and_ready) {
  CHECK(auto_start_due(1, 1, 1, 1, 0, 0));
  CHECK(!auto_start_due(1, 1, 1, 1, 1, 0)); /* user already in the menu */
  CHECK(!auto_start_due(1, 1, 1, 1, 0, 1)); /* only once */
  CHECK(!auto_start_due(1, 0, 1, 1, 0, 0)); /* still discovering */
  CHECK(!auto_start_due(1, 1, 0, 1, 0, 0)); /* no manifest */
  CHECK(!auto_start_due(1, 1, 1, 0, 0, 0)); /* auto_install = no */
  CHECK(!auto_start_due(0, 1, 1, 1, 0, 0)); /* HDD unusable */
}

TEST(batch_resumable_copy_is_selected_and_needs_only_the_channel) {
  batch_entry_t e[3];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_HIDDEN_UNVERIFIED, 4096);
  e[0].resumable = 1;
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_HIDDEN_UNVERIFIED, 4096);
  e[2] = ent("C.iso", ERR_OK, "__.SLUS-20314..C", PAIR_NONE, 1024);
  batch_classify(e, 3);
  CHECK_EQ_INT(e[0].status, BATCH_RESUME);
  CHECK_EQ_INT(e[1].status, BATCH_EXISTS);
  CHECK(e[0].selected && !e[1].selected && e[2].selected);
  CHECK_STR(batch_status_label(BATCH_RESUME), "resume copy");
  /* data partitions already exist: only the 128 MiB channel is new */
  CHECK_EQ_U64(batch_needed_mb(e, 3), 128 + 1024 + 128);
  CHECK_EQ_INT(batch_toggle(&e[0]), 0);
  CHECK_EQ_INT(batch_toggle(&e[0]), 1);
  CHECK_EQ_INT(batch_auto_select(e, 3, 128 + 1152), 2);
  CHECK(e[0].selected && e[2].selected);
  CHECK_EQ_INT(batch_auto_select(e, 3, 128), 1); /* only the resume fits */
  CHECK(e[0].selected && !e[2].selected);
}

TEST(batch_summary_counts_paused) {
  batch_entry_t e[2];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 4096);
  batch_classify(e, 2);
  e[0].result = BATCH_PAUSED;
  e[1].result = BATCH_SKIPPED;
  char sum[512];
  batch_summary(e, 2, sum, sizeof(sum));
  CHECK(strncmp(sum, "0 installed, 0 data only (channel pending), 0 failed, 1 paused, 1 skipped\n", 74) == 0);
  CHECK(strstr(sum, "paused    A.iso") != NULL);
  CHECK(strstr(sum, "Resume copy") != NULL);
}

TEST(batch_summary_mentions_opl_cfg_failure) {
  batch_entry_t e[1];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  batch_classify(e, 1);
  e[0].result = BATCH_DONE;
  e[0].opl_cfg = "failed";
  char sum[512];
  batch_summary(e, 1, sum, sizeof(sum));
  CHECK(strstr(sum, "OPL cfg not copied") != NULL);
}

TEST(batch_summary_mentions_missing_cover) {
  batch_entry_t e[2];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 4096);
  batch_classify(e, 2);
  e[0].result = e[1].result = BATCH_DONE;
  e[0].jacket = "missing"; /* server listed a cover, it could not be read */
  e[1].jacket = "server";
  char sum[512];
  batch_summary(e, 2, sum, sizeof(sum));
  const char *hit = strstr(sum, "cover not found on server");
  CHECK(hit != NULL);
  CHECK(hit && strstr(hit + 1, "cover not found on server") == NULL); /* only A */
}

TEST(batch_summary_mentions_skipped_verification) {
  batch_entry_t e[2];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 4096);
  batch_classify(e, 2);
  e[0].result = e[1].result = BATCH_DONE;
  e[1].verify_skipped = 1;
  char sum[512];
  batch_summary(e, 2, sum, sizeof(sum));
  const char *hit = strstr(sum, "verification skipped");
  CHECK(hit != NULL);
  CHECK(hit && strstr(sum, "B.iso") < hit && strstr(sum, "A.iso") < strstr(sum, "B.iso"));
  CHECK(hit && strstr(hit + 1, "verification skipped") == NULL);
}

TEST(batch_auto_select_fits_free_space_in_order) {
  batch_entry_t e[4];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 4096);
  e[2] = ent("C.iso", ERR_OK, "__.SLUS-20314..C", PAIR_NONE, 512);
  e[3] = ent("D.iso", ERR_OK, "__.SLUS-20315..D", PAIR_COMPLETE, 512);
  batch_classify(e, 4);
  /* 4096+128 fits, next 4224 does not, 512+128 still fits */
  CHECK_EQ_INT(batch_auto_select(e, 4, 4224 + 640), 2);
  CHECK(e[0].selected && !e[1].selected && e[2].selected && !e[3].selected);
  CHECK_EQ_INT(e[1].status, BATCH_NO_SPACE);
  CHECK_EQ_INT(e[3].status, BATCH_EXISTS);
  CHECK(strcmp(batch_status_label(BATCH_NO_SPACE), "no space") == 0);
  CHECK_EQ_INT(batch_auto_select(e, 4, 0), 0);
}

TEST(batch_game_on_hdd_under_other_title_is_exists) {
  /* Installed by an earlier release under the volume-ID title; the server
   * now offers a CFG title, so the pair names differ. Same game ID. */
  batch_entry_t e[2];
  e[0] = ent("GT4.iso", ERR_OK, "__.SLUS-20312..GRAN_TURISMO_4", PAIR_NONE, 4096);
  e[1] = ent("Other.iso", ERR_OK, "__.SLUS-20313..OTHER", PAIR_NONE, 512);
  const char *on_hdd[] = {"__common", "__.SLUS-20312..GT4", "PP.SLUS-20312..GT4",
                          "PP.UDPFS-INSTALLER", "__.SLUS-2031..BAD"};
  batch_mark_on_hdd(e, 2, on_hdd, 5);
  CHECK(e[0].id_on_hdd);
  CHECK(!e[1].id_on_hdd);
  batch_classify(e, 2);
  CHECK_EQ_INT(e[0].status, BATCH_EXISTS);
  CHECK_EQ_INT(e[1].status, BATCH_ELIGIBLE);
  CHECK_EQ_INT(batch_auto_select(e, 2, 100000), 1);
  CHECK(!e[0].selected);
}

TEST(batch_same_id_different_titles_is_duplicate) {
  /* "GTA SA.iso" and "Grand Theft Auto SA.zso", no CFG/gamelist entry:
   * file-name titles differ, the game is the same. */
  batch_entry_t e[2];
  e[0] = ent("GTA SA.iso", ERR_OK, "__.SLUS-20946..GTA_SA", PAIR_NONE, 4096);
  e[1] = ent("Grand Theft Auto SA.zso.iso", ERR_OK, "__.SLUS-20946..GRAND_THEFT_AUTO_SA",
             PAIR_NONE, 4096);
  batch_classify(e, 2);
  CHECK_EQ_INT(e[0].status, BATCH_ELIGIBLE);
  CHECK_EQ_INT(e[1].status, BATCH_DUPLICATE);
  CHECK_EQ_INT(e[1].duplicate_of, 0);
  CHECK_EQ_INT(batch_count_selected(e, 2), 1);
}

TEST(auto_waits_while_server_scans) {
  static manifest_t m;
  const char *scanning = "udpfsd-manifest 1 auto=1 scanning=1\n";
  const char *ready = "udpfsd-manifest 1 auto=1\n";
  CHECK(auto_should_wait(0, NULL));
  CHECK_EQ_INT(manifest_parse(scanning, strlen(scanning), &m), 0);
  CHECK(m.scanning && m.auto_install);
  CHECK(auto_should_wait(1, &m));
  CHECK_EQ_INT(manifest_parse(ready, strlen(ready), &m), 0);
  CHECK(!m.scanning);
  CHECK(!auto_should_wait(1, &m));
}

TEST(auto_installer_partition_never_repaired) {
  CHECK_EQ_INT(auto_installer_step(0, 0), AUTO_CREATE_INSTALLER);
  CHECK_EQ_INT(auto_installer_step(1, 1), AUTO_INSTALLER_OK);
  /* exists but did not mount: may be damaged - stop, never rewrite it */
  CHECK_EQ_INT(auto_installer_step(1, 0), AUTO_STOP);
}

TEST(batch_row_fits_list_width) {
  /* list rows are drawn after a 3-char "  >" marker inside UI_COLS (74) */
  batch_entry_t e = ent("A very long game file name that goes on and on (USA) (v3.00).zso.iso",
                        ERR_SOURCE_INVALID_ISO, "", PAIR_NONE, 0);
  batch_classify(&e, 1);
  char row[128];
  batch_format_row(&e, row, sizeof(row));
  CHECK((int)strlen(row) <= 70);
  CHECK(strstr(row, batch_status_label(BATCH_INVALID)) != NULL);
}
