#include <stdlib.h>
#include <string.h>

#include "../../src/extras.h"
#include "test.h"

TEST(optcfg_replaces_and_appends) {
  char t[256] = "$Compatibility=2\r\n$VMC_0=OLD\r\nTitle=X\r\n";
  int n = (int)strlen(t);
  n = optcfg_set(t, n, (int)sizeof(t), "$VMC_0", "SLUS_200.66_0");
  CHECK(n > 0);
  t[n] = 0;
  CHECK_STR(t, "$Compatibility=2\r\n$VMC_0=SLUS_200.66_0\r\nTitle=X\r\n");
  n = optcfg_set(t, n, (int)sizeof(t), "$EnableCheat", "1");
  t[n] = 0;
  CHECK_STR(t, "$Compatibility=2\r\n$VMC_0=SLUS_200.66_0\r\nTitle=X\r\n$EnableCheat=1\r\n");
  char v[32];
  CHECK_EQ_INT(optcfg_get(t, n, "$VMC_0", v, sizeof(v)), 1);
  CHECK_STR(v, "SLUS_200.66_0");
  CHECK_EQ_INT(optcfg_get(t, n, "$VMC", v, sizeof(v)), 0); /* whole key only */
  CHECK_EQ_INT(optcfg_get(t, n, "$VMC_1", v, sizeof(v)), 0);
}

TEST(optcfg_handles_lf_no_final_newline_and_full_buffer) {
  char t[64] = "a=1\nb=2";
  int n = (int)strlen(t);
  n = optcfg_set(t, n, (int)sizeof(t), "c", "3");
  t[n] = 0;
  CHECK_STR(t, "a=1\nb=2\r\nc=3\r\n");
  n = optcfg_set(t, n, (int)sizeof(t), "a", "10");
  t[n] = 0;
  CHECK_STR(t, "a=10\nb=2\r\nc=3\r\n");
  char small[8] = "";
  CHECK_EQ_INT(optcfg_set(small, 0, (int)sizeof(small), "$EnableCheat", "1"), -1);
  n = optcfg_set(small, 0, (int)sizeof(small), "k", "v");
  CHECK_EQ_INT(n, 5);
}

TEST(extra_names_and_kinds) {
  char id[16];
  int slot;
  CHECK_EQ_INT(extra_name_id("SLUS_200.66_1.bin", id, &slot), 0);
  CHECK_STR(id, "SLUS_200.66");
  CHECK_EQ_INT(slot, 1);
  CHECK_EQ_INT(extra_name_id("slus-20066.VMC", id, &slot), 0);
  CHECK_STR(id, "SLUS_200.66");
  CHECK_EQ_INT(slot, 0);
  CHECK_EQ_INT(extra_name_id("SCES12345_SLOT1.mcr", id, &slot), 0);
  CHECK_STR(id, "SCES_123.45");
  CHECK_EQ_INT(slot, 1);
  CHECK_EQ_INT(extra_name_id("/VMC/SLUS_200.66.cht", id, &slot), 0);
  CHECK_EQ_INT(extra_name_id("SLUS_200.6.bin", id, &slot), -1);
  CHECK_EQ_INT(extra_name_id("SLUS_2000066.bin", id, &slot), -1);
  CHECK_EQ_INT(extra_name_id("Gran Turismo.bin", id, &slot), -1);
  CHECK_EQ_INT(extra_classify("x.BIN"), EXTRA_PS2_VMC);
  CHECK_EQ_INT(extra_classify("x.ps2"), EXTRA_PS2_VMC);
  CHECK_EQ_INT(extra_classify("x.psu"), EXTRA_PS2_SAVE);
  CHECK_EQ_INT(extra_classify("x.gme"), EXTRA_PS1_CARD);
  CHECK_EQ_INT(extra_classify("x.mcs"), EXTRA_PS1_SAVE);
  CHECK_EQ_INT(extra_classify("x.cht"), EXTRA_PS2_CHEAT);
  CHECK_EQ_INT(extra_classify("x.txt"), EXTRA_PS1_CHEAT);
  CHECK_EQ_INT(extra_classify("x.iso"), EXTRA_NONE);
}

static void superblock(uint8_t *h, uint32_t clusters) {
  memset(h, 0, 0x154);
  memcpy(h, "Sony PS2 Memory Card Format 1.2.0.0", 35);
  h[0x28] = 0x00;
  h[0x29] = 0x02; /* page 512 */
  h[0x2A] = 2;    /* pages per cluster */
  h[0x2C] = 16;
  h[0x30] = (uint8_t)clusters;
  h[0x31] = (uint8_t)(clusters >> 8);
  h[0x150] = 2;
}

TEST(ps2_card_kinds_and_ecc_strip) {
  uint8_t h[0x154];
  superblock(h, 8192); /* 8 MiB */
  CHECK_EQ_INT(ps2vmc_kind(h, sizeof(h), 8388608), PS2VMC_RAW);
  CHECK_EQ_INT(ps2vmc_kind(h, sizeof(h), 8650752), PS2VMC_ECC);
  CHECK_EQ_U64(ps2vmc_raw_size(PS2VMC_ECC, 8650752), 8388608);
  CHECK_EQ_INT(ps2vmc_kind(h, sizeof(h), 8388608 + 512), PS2VMC_BAD);
  h[0x150] = 1;
  CHECK_EQ_INT(ps2vmc_kind(h, sizeof(h), 8388608), PS2VMC_BAD);
  h[0x150] = 2;
  h[0] = 'X';
  CHECK_EQ_INT(ps2vmc_kind(h, sizeof(h), 8388608), PS2VMC_BAD);
  static uint8_t in[528 * 3], out[512 * 3];
  for (int p = 0; p < 3; p++) {
    memset(in + 528 * p, 'a' + p, 512);
    memset(in + 528 * p + 512, 0xEE, 16); /* ECC */
  }
  CHECK_EQ_INT(ps2vmc_strip_ecc(in, sizeof(in), out), 1536);
  CHECK(out[0] == 'a' && out[511] == 'a' && out[512] == 'b' && out[1535] == 'c');
}

static uint8_t *make_mcs(const char *name, int blocks, uint8_t fill, uint32_t *len) {
  *len = PS1_FRAME + PS1_BLOCK * (uint32_t)blocks;
  uint8_t *m = calloc(1, *len);
  m[0] = 0x51;
  strncpy((char *)m + 0x0A, name, 20);
  memset(m + PS1_FRAME, fill, PS1_BLOCK * (uint32_t)blocks);
  m[PS1_FRAME] = 'S';
  m[PS1_FRAME + 1] = 'C';
  return m;
}

static int frame_ok(const uint8_t *f) {
  uint8_t x = 0;
  for (int i = 0; i < 127; i++)
    x ^= f[i];
  return x == f[127];
}

TEST(ps1_card_format_and_insert) {
  static uint8_t card[PS1_CARD_SIZE];
  ps1card_format(card);
  CHECK(ps1card_valid(card));
  CHECK_EQ_INT(card[127], 0x0E); /* the usual header checksum */
  CHECK_EQ_INT(ps1card_free_blocks(card), 15);
  CHECK(memcmp(card, card + PS1_FRAME * 63, PS1_FRAME) == 0);
  uint32_t len;
  uint8_t *a = make_mcs("BASLUS-00067GAME1", 3, 0x5A, &len);
  CHECK_EQ_INT(ps1card_insert(card, a, len, 0), 0);
  CHECK_EQ_INT(ps1card_free_blocks(card), 12);
  /* Blocks 1..3, linked 0 -> 1 -> 2, sizes and checksums. */
  CHECK_EQ_INT(card[PS1_FRAME * 1], 0x51);
  CHECK_EQ_INT(card[PS1_FRAME * 2], 0x52);
  CHECK_EQ_INT(card[PS1_FRAME * 3], 0x53);
  CHECK_EQ_INT(card[PS1_FRAME * 1 + 4] | card[PS1_FRAME * 1 + 5] << 8, 3 * 8192 & 0xFFFF);
  CHECK_EQ_INT(card[PS1_FRAME * 1 + 6], 0);
  CHECK_EQ_INT(card[PS1_FRAME * 1 + 8], 1);
  CHECK_EQ_INT(card[PS1_FRAME * 2 + 8], 2);
  CHECK_EQ_INT(card[PS1_FRAME * 3 + 8], 0xFF);
  CHECK(frame_ok(card + PS1_FRAME * 1) && frame_ok(card + PS1_FRAME * 2) &&
        frame_ok(card + PS1_FRAME * 3));
  CHECK(card[PS1_BLOCK * 1] == 'S' && card[PS1_BLOCK * 2 + 5] == 0x5A);
  /* Same name again: refused, or replaced in place. */
  CHECK_EQ_INT(ps1card_insert(card, a, len, 0), -3);
  CHECK_EQ_INT(ps1card_insert(card, a, len, 1), 0);
  CHECK_EQ_INT(ps1card_free_blocks(card), 12);
  free(a);
  /* Not enough room. */
  uint8_t *b = make_mcs("BASLUS-00067GAME2", 13, 1, &len);
  CHECK_EQ_INT(ps1card_insert(card, b, len, 0), -2);
  free(b);
  /* Not an .mcs */
  uint8_t junk[PS1_FRAME + PS1_BLOCK] = {0};
  CHECK_EQ_INT(ps1card_insert(card, junk, sizeof(junk), 0), -1);
}

TEST(ps1_card_file_headers) {
  static uint8_t f[0xF40 + PS1_CARD_SIZE];
  memset(f, 0, sizeof(f));
  f[0] = 'M';
  f[1] = 'C';
  CHECK_EQ_INT(ps1card_offset(f, PS1_CARD_SIZE), 0);
  memcpy(f, "123-456-STD", 11);
  CHECK_EQ_INT(ps1card_offset(f, sizeof(f)), 0xF40);
  memcpy(f, "\0PMV", 4);
  CHECK_EQ_INT(ps1card_offset(f, 0x80 + PS1_CARD_SIZE), 0x80);
  CHECK_EQ_INT(ps1card_offset(f, 1000), -1);
}

static void psu_ent(uint8_t *e, uint16_t mode, uint32_t size, const char *name) {
  memset(e, 0, 512);
  e[0] = (uint8_t)mode;
  e[1] = (uint8_t)(mode >> 8);
  e[4] = (uint8_t)size;
  e[5] = (uint8_t)(size >> 8);
  strcpy((char *)e + 0x40, name);
}

TEST(psu_parses_folder_and_files) {
  static uint8_t d[512 * 3 + (512 + 1024) * 2];
  memset(d, 0, sizeof(d));
  psu_ent(d, 0x8427, 4, "BASLUS-20066GT");
  psu_ent(d + 512, 0x8427, 0, ".");
  psu_ent(d + 1024, 0x8427, 0, "..");
  psu_ent(d + 1536, 0x8497, 100, "icon.sys");
  memset(d + 2048, 'i', 100);
  psu_ent(d + 2048 + 1024, 0x8497, 1024, "BASLUS-20066GT");
  char dir[33];
  psu_entry_t e[8];
  int n = psu_parse(d, sizeof(d), dir, e, 8);
  CHECK_EQ_INT(n, 2);
  CHECK_STR(dir, "BASLUS-20066GT");
  CHECK_STR(e[0].name, "icon.sys");
  CHECK_EQ_INT(e[0].size, 100);
  CHECK_EQ_INT(e[0].data_off, 2048);
  CHECK_EQ_INT(e[1].data_off, 2048 + 1024 + 512);
  CHECK_EQ_INT(psu_parse(d, sizeof(d), dir, e, 1), -1); /* too many files */
  psu_ent(d + 1536, 0x8427, 0, "sub"); /* a subfolder */
  CHECK_EQ_INT(psu_parse(d, sizeof(d), dir, e, 8), -1);
  psu_ent(d + 1536, 0x8497, 100000, "big"); /* beyond the file */
  CHECK_EQ_INT(psu_parse(d, sizeof(d), dir, e, 8), -1);
}
