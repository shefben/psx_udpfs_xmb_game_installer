#include "../../src/pops.h"
#include "test.h"

#define VCD_HDR 0x100000u
#define RAW 2352u

static uint8_t pvd[2048], root[2048], cnf[2048];

/* A fake VCD: 1 MiB header + `nsect` raw 2352-byte sectors; PVD at 16,
 * root directory at 18 (SYSTEM.CNF -> sector 20). */
static uint64_t build_vcd(GameSource *src, memsrc_t *m, const char *cnf_text, int nsect) {
  memset(pvd, 0, sizeof(pvd));
  memset(root, 0, sizeof(root));
  memset(cnf, 0, sizeof(cnf));
  memcpy(pvd, "\x01" "CD001", 6);
  memset(pvd + 40, ' ', 32);
  memcpy(pvd + 40, "METALGEARSOLID", 14);
  pvd[156] = 34;
  pvd[156 + 2] = 18; /* root extent LBA (le32) */
  pvd[156 + 10] = 0x00;
  pvd[156 + 11] = 0x08; /* root size 2048 */
  /* "." entry, then SYSTEM.CNF;1 */
  root[0] = 34;
  root[32] = 1;
  uint8_t *r = root + 34;
  const char *nm = "SYSTEM.CNF;1";
  r[0] = (uint8_t)(33 + strlen(nm) + 1);
  r[2] = 20; /* extent LBA */
  r[10] = (uint8_t)strlen(cnf_text);
  r[32] = (uint8_t)strlen(nm);
  memcpy(r + 33, nm, strlen(nm));
  memcpy(cnf, cnf_text, strlen(cnf_text));
  uint64_t size = VCD_HDR + (uint64_t)nsect * RAW;
  memsrc_init(src, m, size);
  memsrc_add(m, VCD_HDR + 16ull * RAW + 24, pvd, sizeof(pvd));
  memsrc_add(m, VCD_HDR + 18ull * RAW + 24, root, sizeof(root));
  memsrc_add(m, VCD_HDR + 20ull * RAW + 24, cnf, sizeof(cnf));
  return size;
}

TEST(vcd_probe_reads_boot_id_and_volume) {
  GameSource src;
  memsrc_t m;
  build_vcd(&src, &m, "BOOT = cdrom:\\SLUS_005.94;1\r\nTCB = 4\r\n", 300);
  vcd_info_t v;
  CHECK_EQ_INT(vcd_probe(&src, &v), ERR_OK);
  CHECK_STR(v.boot_id, "SLUS_005.94");
  CHECK_STR(v.part_id, "SLUS-00594");
  CHECK_STR(v.volume_id, "METALGEARSOLID");
  CHECK_EQ_INT(v.sectors, 300);
}

TEST(vcd_probe_boot_without_backslash) {
  GameSource src;
  memsrc_t m;
  build_vcd(&src, &m, "BOOT=cdrom:SCUS_944.26;1\n", 40);
  vcd_info_t v;
  CHECK_EQ_INT(vcd_probe(&src, &v), ERR_OK);
  CHECK_STR(v.boot_id, "SCUS_944.26");
}

TEST(vcd_probe_rejects_non_vcd) {
  GameSource src;
  memsrc_t m;
  vcd_info_t v;
  build_vcd(&src, &m, "BOOT = cdrom:\\SLUS_005.94;1\n", 300);
  m.total_size -= 1; /* not header + whole raw sectors */
  CHECK_EQ_INT(vcd_probe(&src, &v), ERR_SOURCE_INVALID_ISO);
  build_vcd(&src, &m, "BOOT = cdrom:\\PSX.EXE;1\n", 300); /* no game ID */
  CHECK_EQ_INT(vcd_probe(&src, &v), ERR_SOURCE_SYSTEM_CNF);
  build_vcd(&src, &m, "BOOT = cdrom:\\SLUS_005.94;1\n", 300);
  pvd[1] = 'X'; /* no PVD */
  CHECK_EQ_INT(vcd_probe(&src, &v), ERR_SOURCE_INVALID_ISO);
}

TEST(pops_partition_size_steps) {
  char s[8];
  CHECK_EQ_INT(pops_partition_mb(100ull << 20, s, sizeof(s)), 128);
  CHECK_STR(s, "128M");
  CHECK_EQ_INT(pops_partition_mb(124ull << 20, s, sizeof(s)), 256); /* + 8 MiB slack */
  CHECK_EQ_INT(pops_partition_mb(700ull << 20, s, sizeof(s)), 1024);
  CHECK_STR(s, "1G");
  CHECK_EQ_INT(pops_partition_mb(1020ull << 20, s, sizeof(s)), 2048);
  CHECK_STR(s, "2G");
  /* Multi-disc games: all discs in one partition. */
  CHECK_EQ_INT(pops_partition_mb(3000ull << 20, s, sizeof(s)), 4096);
  CHECK_STR(s, "4G");
  CHECK_EQ_INT(pops_partition_mb(4090ull << 20, s, sizeof(s)), -1);
}

TEST(pops_disc_numbers_and_stems) {
  char a[64], b[64];
  CHECK_EQ_INT(pops_disc_number("udpfs:/POPS/Final Fantasy VII (USA) (Disc 1).VCD", a, sizeof(a)), 1);
  CHECK_EQ_INT(pops_disc_number("Final Fantasy VII (USA) (Disc 3).vcd", b, sizeof(b)), 3);
  CHECK_STR(a, "Final Fantasy VII (USA)");
  CHECK_STR(a, b);
  CHECK_EQ_INT(pops_disc_number("Parasite Eve CD2.VCD", a, sizeof(a)), 2);
  CHECK_STR(a, "Parasite Eve");
  CHECK_EQ_INT(pops_disc_number("Xenogears_disc_1.VCD", a, sizeof(a)), 1);
  CHECK_STR(a, "Xenogears");
  CHECK_EQ_INT(pops_disc_number("Metal Gear Solid.VCD", a, sizeof(a)), 0);
  CHECK_STR(a, "Metal Gear Solid");
  CHECK_EQ_INT(pops_disc_number("Discworld.VCD", a, sizeof(a)), 0);   /* a word, not "Disc N" */
  CHECK_EQ_INT(pops_disc_number("ABCD 2.VCD", a, sizeof(a)), 0);      /* "CD" inside a word */
  CHECK_EQ_INT(pops_disc_number("Game (Disc 5).VCD", a, sizeof(a)), 0); /* POPStarter: 4 */
  CHECK_EQ_INT(pops_disc_number("Game Disc 12.VCD", a, sizeof(a)), 0);
}

TEST(pops_discs_txt_lists_images) {
  char t[80];
  CHECK_EQ_INT(pops_discs_txt(3, t, sizeof(t)), 36);
  CHECK_STR(t, "IMAGE0.VCD\r\nIMAGE1.VCD\r\nIMAGE2.VCD\r\n");
  CHECK_EQ_INT(pops_discs_txt(1, t, sizeof(t)), -1);
  CHECK_EQ_INT(pops_discs_txt(5, t, sizeof(t)), -1);
  CHECK_STR(pops_image_name(3), "IMAGE3.VCD");
  CHECK(pops_image_name(4) == NULL);
}

TEST(pops_vmc_folder_and_names) {
  char d[40];
  pops_vmc_dir("PP.SLUS-00594..METAL GEAR", d, sizeof(d));
  CHECK_STR(d, "SLUS-00594..METAL GEAR");
  CHECK(source_classify("Metal Gear Solid.VCD") == SRC_TYPE_VCD);
  CHECK(source_classify("x.vcd") == SRC_TYPE_VCD);
  CHECK_STR(source_type_label(SRC_TYPE_VCD), "PS1");
}
