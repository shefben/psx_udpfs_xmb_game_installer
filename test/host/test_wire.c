#include <string.h>

#include "../../src/source.h"
#include "../../src/wire_frame.h"
#include "test.h"

static void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* One LZ4 block for 2048 zero bytes: literal 0, match offset 1 of 2042,
 * then 5 literals (LZ4 ends with literals). */
static uint32_t zeros_block(uint8_t *b) {
  uint32_t k = 0;
  b[k++] = 0x1F; /* 1 literal, match length nibble 15 */
  b[k++] = 0;
  b[k++] = 1;
  b[k++] = 0;
  for (int i = 0; i < 7; i++)
    b[k++] = 255;
  b[k++] = 238; /* 15 + 7*255 + 238 + 4 = 2042 */
  b[k++] = 0x50;
  for (int i = 0; i < 5; i++)
    b[k++] = 0;
  return k;
}

static uint32_t lz4_frame(uint8_t *f, uint32_t raw_len, const uint8_t *data, uint32_t n) {
  wr32(f, WIRE_MAGIC);
  wr32(f + 4, raw_len);
  wr32(f + 8, n);
  wr32(f + 12, WIRE_F_LZ4);
  memcpy(f + WIRE_HDR, data, n);
  uint32_t padded = (n + 15) & ~15u;
  memset(f + WIRE_HDR + n, 0, padded - n);
  return WIRE_HDR + padded;
}

TEST(wire_raw_bytes_per_request) {
  CHECK_EQ_INT(wire_frame_raw_for(128 * 1024), 129024); /* 63 sectors */
  CHECK_EQ_INT(wire_frame_raw_for(64 * 1024), 63488);
  CHECK_EQ_INT(wire_frame_raw_for(2048 + 16), 2048);
  CHECK_EQ_INT(wire_frame_raw_for(2048 + 15), 0);
  CHECK_EQ_INT(wire_frame_raw_for(16), 0);
}

TEST(wire_stored_frame_round_trip) {
  static uint8_t raw[4096], f[4096 + 64], out[4096];
  for (int i = 0; i < 4096; i++)
    raw[i] = (uint8_t)(i * 7);
  uint32_t t = wire_frame_build_stored(raw, 4096, f);
  CHECK_EQ_INT(t, 16 + 4096);
  wire_frame_t w;
  CHECK_EQ_INT(wire_frame_parse(f, t, 4096, &w), (int)t);
  CHECK_EQ_INT(w.raw_len, 4096);
  CHECK(w.data == f + 16);
  CHECK_EQ_INT(wire_frame_decode(&w, out), 0);
  CHECK(memcmp(out, raw, 4096) == 0);
  /* raw_len over the request's maximum is a framing error */
  CHECK_EQ_INT(wire_frame_parse(f, t, 2048, &w), -1);
}

TEST(wire_lz4_frame_decodes) {
  uint8_t blk[32], f[64];
  static uint8_t out[2048];
  uint32_t n = zeros_block(blk);
  uint32_t t = lz4_frame(f, 2048, blk, n);
  CHECK_EQ_INT(t % 16, 0);
  wire_frame_t w;
  CHECK_EQ_INT(wire_frame_parse(f, t, 126976, &w), (int)t);
  memset(out, 0xAA, sizeof(out));
  CHECK_EQ_INT(wire_frame_decode(&w, out), 0);
  int zero = 1;
  for (int i = 0; i < 2048; i++)
    zero &= out[i] == 0;
  CHECK(zero);
  /* A frame that claims more raw bytes than the block holds fails. */
  lz4_frame(f, 4096, blk, n);
  CHECK_EQ_INT(wire_frame_parse(f, t, 126976, &w), (int)t);
  static uint8_t big[4096];
  CHECK_EQ_INT(wire_frame_decode(&w, big), -1);
}

TEST(wire_rejects_malformed_frames) {
  static uint8_t raw[2048], f[2048 + 64];
  uint32_t t = wire_frame_build_stored(raw, 2048, f);
  wire_frame_t w;
  CHECK_EQ_INT(wire_frame_parse(f, 15, 2048, &w), -1); /* short header */
  f[0] ^= 1;
  CHECK_EQ_INT(wire_frame_parse(f, t, 2048, &w), -1); /* magic */
  f[0] ^= 1;
  wr32(f + 12, 2);
  CHECK_EQ_INT(wire_frame_parse(f, t, 2048, &w), -1); /* unknown flag */
  wr32(f + 12, 0);
  wr32(f + 8, 2047);
  CHECK_EQ_INT(wire_frame_parse(f, t, 2048, &w), -1); /* stored but data != raw */
  wr32(f + 8, 4096);
  wr32(f + 4, 4096);
  CHECK_EQ_INT(wire_frame_parse(f, t, 8192, &w), -1); /* data beyond what arrived */
  wr32(f + 4, 0);
  wr32(f + 8, 0);
  CHECK_EQ_INT(wire_frame_parse(f, t, 2048, &w), -1); /* empty frame */
}

TEST(wire_two_frames_in_one_read) {
  static uint8_t a[2048], b[4096], buf[8192];
  memset(a, 1, sizeof(a));
  memset(b, 2, sizeof(b));
  uint32_t t1 = wire_frame_build_stored(a, sizeof(a), buf);
  uint32_t t2 = wire_frame_build_stored(b, sizeof(b), buf + t1);
  wire_frame_t w;
  CHECK_EQ_INT(wire_frame_parse(buf, t1 + t2, 4096, &w), (int)t1);
  CHECK_EQ_INT(w.raw_len, 2048);
  CHECK_EQ_INT(wire_frame_parse(buf + w.total, t1 + t2 - w.total, 4096, &w), (int)t2);
  CHECK_EQ_INT(w.raw_len, 4096);
}

TEST(wire_path_prefix) {
  char out[64];
  CHECK_EQ_INT(wire_path("udpfs:/DVD/a.iso", out, sizeof(out)), 0);
  CHECK_STR(out, "udpfs:/.lz4f/DVD/a.iso");
  CHECK_EQ_INT(wire_path("udpfs:DVD/a.iso", out, sizeof(out)), 0);
  CHECK_STR(out, "udpfs:/.lz4f/DVD/a.iso");
  CHECK_EQ_INT(wire_path("mass0:/DVD/a.iso", out, sizeof(out)), -1);
  CHECK_EQ_INT(wire_path("udpfs:/", out, sizeof(out)), -1);
  CHECK_EQ_INT(wire_path("udpfs:/DVD/a.iso", out, 20), -1);
}

/* ---- source_next_block -------------------------------------------- */

typedef struct {
  uint64_t pos, size;
  uint32_t lend; /* bytes read_ptr hands out per call */
  uint8_t buf[16384];
} lender_t;

static uint8_t pat(uint64_t off) { return (uint8_t)(off * 31 + 7); }

static int l_read(GameSource *s, void *buf, uint32_t n) {
  lender_t *l = s->priv;
  if (l->pos >= l->size)
    return 0;
  if (n > l->size - l->pos)
    n = (uint32_t)(l->size - l->pos);
  for (uint32_t i = 0; i < n; i++)
    ((uint8_t *)buf)[i] = pat(l->pos + i);
  l->pos += n;
  return (int)n;
}

static int l_read_ptr(GameSource *s, uint32_t max, const uint8_t **out) {
  lender_t *l = s->priv;
  uint32_t n = max < l->lend ? max : l->lend;
  int r = l_read(s, l->buf, n);
  *out = l->buf;
  return r;
}

static int64_t l_seek(GameSource *s, int64_t off, int whence) {
  (void)whence;
  ((lender_t *)s->priv)->pos = (uint64_t)off;
  return off;
}

static int64_t l_size(GameSource *s) { return (int64_t)((lender_t *)s->priv)->size; }

static const GameSourceOps L_OPS = {NULL, NULL, l_read, l_seek, l_size, NULL, l_read_ptr};

TEST(next_block_lends_whole_sectors) {
  static lender_t l;
  memset(&l, 0, sizeof(l));
  l.size = 10000;
  l.lend = 4096;
  GameSource s = {&L_OPS, &l, 1, 0, ""};
  static uint8_t buf[8192];
  const uint8_t *p;
  uint32_t got;
  CHECK_EQ_INT(source_next_block(&s, buf, 8192, &p, &got), ERR_OK);
  CHECK_EQ_INT(got, 4096); /* lent, a whole number of sectors */
  CHECK(p == l.buf);
  CHECK(p[0] == pat(0) && p[4095] == pat(4095));
}

TEST(next_block_completes_a_partial_sector) {
  static lender_t l;
  memset(&l, 0, sizeof(l));
  l.size = 100000;
  l.lend = 3000; /* not a sector multiple: completed in buf */
  GameSource s = {&L_OPS, &l, 1, 0, ""};
  static uint8_t buf[8192];
  const uint8_t *p;
  uint32_t got;
  CHECK_EQ_INT(source_next_block(&s, buf, 8192, &p, &got), ERR_OK);
  CHECK_EQ_INT(got, 8192);
  CHECK(p == buf);
  int ok = 1;
  for (int i = 0; i < 8192; i++)
    ok &= buf[i] == pat((uint64_t)i);
  CHECK(ok);
  CHECK_EQ_U64(l.pos, 8192);
}

TEST(next_block_last_short_block_and_eof) {
  static lender_t l;
  memset(&l, 0, sizeof(l));
  l.size = 5000;
  l.lend = 8192;
  GameSource s = {&L_OPS, &l, 1, 0, ""};
  static uint8_t buf[8192];
  const uint8_t *p;
  uint32_t got;
  /* The caller asks for exactly what is left: 5000 bytes. */
  CHECK_EQ_INT(source_next_block(&s, buf, 5000, &p, &got), ERR_OK);
  CHECK_EQ_INT(got, 5000);
  CHECK_EQ_INT(source_next_block(&s, buf, 2048, &p, &got), ERR_SOURCE_READ); /* EOF */
}

TEST(next_block_without_lending_reads_into_buf) {
  static memsrc_t m;
  GameSource s;
  memsrc_init(&s, &m, 1 << 20);
  m.max_chunk = 1000; /* short reads are looped */
  static uint8_t buf[8192];
  const uint8_t *p;
  uint32_t got;
  CHECK_EQ_INT(source_next_block(&s, buf, 8192, &p, &got), ERR_OK);
  CHECK(p == buf);
  CHECK_EQ_INT(got, 8192);
}
