#include <stdlib.h>

#include "../../src/lz4_block.h"
#include "../../src/source_zso.h"
#include "test.h"

TEST(lz4_literals_and_overlapping_match) {
  /* "abc" literals, then match offset 3 length 9 -> "abcabcabcabc", then
   * literal-only last sequence "xyz". */
  static const uint8_t in[] = {0x35, 'a', 'b', 'c', 0x03, 0x00, 0x30, 'x', 'y', 'z'};
  uint8_t out[32];
  int n = lz4_block_decompress(in, sizeof(in), out, sizeof(out));
  CHECK_EQ_INT(n, 15);
  CHECK(n == 15 && memcmp(out, "abcabcabcabcxyz", 15) == 0);
}

TEST(lz4_long_lengths_use_extra_bytes) {
  /* 20 literals: token high nibble 15, extra byte 5; then match offset 1
   * length 4+15+270 (two extra bytes 255, 15) = 289 of the last byte. */
  uint8_t in[64], out[400];
  size_t k = 0;
  in[k++] = 0xFF;
  in[k++] = 5;
  for (int i = 0; i < 20; i++)
    in[k++] = (uint8_t)('A' + i);
  in[k++] = 1;
  in[k++] = 0;
  in[k++] = 255;
  in[k++] = 15;
  int n = lz4_block_decompress(in, k, out, sizeof(out));
  CHECK_EQ_INT(n, 20 + 289);
  int ok = 1;
  for (int i = 20; i < n; i++)
    ok &= out[i] == 'T';
  CHECK(ok);
}

TEST(lz4_rejects_bad_input) {
  uint8_t out[16];
  static const uint8_t too_long[] = {0x50, 'a', 'b'};                /* 5 literals, 2 given */
  static const uint8_t bad_off[] = {0x10, 'a', 0x05, 0x00, 0x00};    /* offset beyond output */
  static const uint8_t zero_off[] = {0x10, 'a', 0x00, 0x00, 0x00};   /* offset 0 */
  static const uint8_t big[] = {0xF0, 0x20};                          /* 47 literals > out */
  CHECK(lz4_block_decompress(too_long, sizeof(too_long), out, sizeof(out)) < 0);
  CHECK(lz4_block_decompress(bad_off, sizeof(bad_off), out, sizeof(out)) < 0);
  CHECK(lz4_block_decompress(zero_off, sizeof(zero_off), out, sizeof(out)) < 0);
  CHECK(lz4_block_decompress(big, sizeof(big), out, sizeof(out)) < 0);
}

/* Build a ZISO image (block size 2048, align 0) of `blocks` blocks in a
 * malloc'd buffer: even blocks stored plain (index bit 31), odd blocks
 * as one LZ4 literal run + a long match of their first byte. */
static uint8_t *make_ziso(int blocks, size_t *len_out, uint8_t *expect) {
  size_t idx_off = 0x18, data_off = idx_off + 4 * (size_t)(blocks + 1);
  uint8_t *z = calloc(1, data_off + (size_t)blocks * 2048 + 64);
  memcpy(z, "ZISO", 4);
  z[4] = 0x18;
  uint64_t total = (uint64_t)blocks * 2048;
  memcpy(z + 8, &total, 8);
  uint32_t bs = 2048;
  memcpy(z + 16, &bs, 4);
  z[20] = 1; /* version */
  z[21] = 0; /* align */
  size_t pos = data_off;
  for (int b = 0; b < blocks; b++) {
    uint8_t *blk = expect + (size_t)b * 2048;
    uint32_t ent = (uint32_t)pos;
    if (b % 2 == 0) {
      for (int i = 0; i < 2048; i++)
        blk[i] = (uint8_t)(b * 7 + i);
      memcpy(z + pos, blk, 2048);
      pos += 2048;
      ent |= 0x80000000u;
    } else {
      /* 1 literal, then match offset 1 for 2047 bytes = 4 + 15 + 2028 */
      memset(blk, 'a' + b, 2048);
      z[pos++] = 0x1F;
      z[pos++] = (uint8_t)('a' + b);
      z[pos++] = 1;
      z[pos++] = 0;
      size_t rest = 2047 - 4 - 15;
      while (rest >= 255) {
        z[pos++] = 255;
        rest -= 255;
      }
      z[pos++] = (uint8_t)rest;
    }
    memcpy(z + idx_off + 4 * (size_t)b, &ent, 4);
  }
  uint32_t end = (uint32_t)pos;
  memcpy(z + idx_off + 4 * (size_t)blocks, &end, 4);
  *len_out = pos;
  return z;
}

TEST(zso_source_reads_any_range_like_the_plain_image) {
  enum { BLOCKS = 9 };
  static uint8_t expect[BLOCKS * 2048];
  size_t zlen;
  uint8_t *z = make_ziso(BLOCKS, &zlen, expect);
  GameSource inner, src;
  memsrc_t m;
  memsrc_init(&inner, &m, zlen);
  memsrc_add(&m, 0, z, (uint32_t)zlen);
  m.max_chunk = 700; /* inner reads may be short */
  zso_src_t zs;
  source_zso_init(&src, &zs, &inner);
  CHECK_EQ_INT(source_open(&src, "mass0:/X.zso"), ERR_OK);
  CHECK_EQ_INT(source_size(&src), BLOCKS * 2048);
  static uint8_t got[BLOCKS * 2048];
  CHECK_EQ_INT(source_read_exact(&src, got, sizeof(got)), ERR_OK);
  CHECK(memcmp(got, expect, sizeof(got)) == 0);
  /* unaligned range across a plain and a compressed block */
  uint8_t part[3000];
  CHECK_EQ_INT(source_read_at(&src, 2048 - 100, part, sizeof(part)), ERR_OK);
  CHECK(memcmp(part, expect + 2048 - 100, sizeof(part)) == 0);
  /* past the end: EOF */
  CHECK_EQ_INT(src.ops->seek(&src, BLOCKS * 2048, SRC_SEEK_SET), BLOCKS * 2048);
  CHECK_EQ_INT(src.ops->read(&src, part, 10), 0);
  source_close(&src);
  free(z);
}

/* Inner source that records the path it was opened with. */
static char opened[SOURCE_PATH_MAX];
static memsrc_t *rec_m;
static int rec_open(GameSource *s, const char *path) {
  (void)s;
  snprintf(opened, sizeof(opened), "%s", path);
  rec_m->pos = 0;
  return 0;
}

TEST(zso_source_strip_iso_opens_the_raw_file) {
  enum { BLOCKS = 3 };
  static uint8_t expect[BLOCKS * 2048];
  size_t zlen;
  uint8_t *z = make_ziso(BLOCKS, &zlen, expect);
  GameSource inner, src;
  memsrc_t m;
  memsrc_init(&inner, &m, zlen);
  memsrc_add(&m, 0, z, (uint32_t)zlen);
  GameSourceOps ops = *inner.ops;
  ops.open = rec_open;
  inner.ops = &ops;
  rec_m = &m;
  zso_src_t zs;
  source_zso_init(&src, &zs, &inner);
  zs.strip_iso = 1;
  CHECK_EQ_INT(source_open(&src, "udpfs:/DVD/Game.zso.iso"), ERR_OK);
  CHECK_STR(opened, "udpfs:/DVD/Game.zso");
  CHECK_STR(src.path, "udpfs:/DVD/Game.zso.iso"); /* logical path unchanged */
  uint8_t got[BLOCKS * 2048];
  CHECK_EQ_INT(source_read_exact(&src, got, sizeof(got)), ERR_OK);
  CHECK(memcmp(got, expect, sizeof(got)) == 0);
  free(z);
}

TEST(zso_source_rejects_bad_header) {
  static const uint8_t junk[64] = "NOTZ";
  GameSource inner, src;
  memsrc_t m;
  memsrc_init(&inner, &m, sizeof(junk));
  memsrc_add(&m, 0, junk, sizeof(junk));
  zso_src_t zs;
  source_zso_init(&src, &zs, &inner);
  CHECK(source_open(&src, "mass0:/bad.zso") != ERR_OK);
}

TEST(source_type_raw_zso_file) {
  CHECK_EQ_INT(source_classify("Game.zso"), SRC_TYPE_ZSO_FILE);
  CHECK_EQ_INT(source_classify("Game.ZSO"), SRC_TYPE_ZSO_FILE);
  CHECK_EQ_INT(source_classify("Game.zso.iso"), SRC_TYPE_ZSO);
  CHECK_STR(source_type_label(SRC_TYPE_ZSO_FILE), "ZSO");
  CHECK(source_is_raw_zso("mass0:/A/Game.zso"));
  CHECK(!source_is_raw_zso("udpfs:/DVD/Game.zso.iso"));
}
