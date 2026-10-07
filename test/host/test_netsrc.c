/* The network sources over a fake fileXio (fake_filexio.c): read-ahead
 * and LZ4 frames must hand out exactly the image's bytes, at the right
 * offsets, whatever the block sizes, seeks and failures. A wrong byte
 * here would be copied AND verified (the CRC is taken over what arrives),
 * so these are the tests that keep an install honest. */
#include <stdlib.h>
#include <string.h>

#include "fake/fileXio_rpc.h"
#include "source.h"
#include "source_udpfs.h"
#include "source_wire.h"
#include "test.h"

static int idle_calls;
static void idle(void *ctx) {
  (void)ctx;
  idle_calls++;
}

/* Copy-loop pattern: whole blocks of `blk` through source_next_block,
 * checking every byte against the image. Returns bytes checked or -1. */
static int64_t drain(GameSource *s, uint64_t from, uint64_t total, uint32_t blk) {
  static uint8_t buf[1 << 20];
  uint64_t pos = from;
  while (pos < total) {
    uint32_t want = total - pos > blk ? blk : (uint32_t)(total - pos);
    const uint8_t *p;
    uint32_t got;
    if (source_next_block(s, buf, want, &p, &got) != ERR_OK)
      return -1;
    if (got == 0 || got > want)
      return -1;
    for (uint32_t i = 0; i < got; i++)
      if (p[i] != fake_byte(pos + i))
        return -1;
    pos += got;
  }
  return (int64_t)(pos - from);
}

static void open_udpfs(GameSource *s, udpfs_src_t *u, uint64_t size) {
  fake_fxio_reset(size, 0);
  source_udpfs_init(s, u);
  CHECK_EQ_INT(source_open(s, "udpfs:/DVD/x.iso"), ERR_OK);
}

TEST(udpfs_readahead_gives_exact_bytes) {
  static GameSource s;
  static udpfs_src_t u;
  uint64_t size = 5 * 1024 * 1024 + 2048 * 7;
  open_udpfs(&s, &u, size);
  idle_calls = 0;
  source_async(&s, SRC_ASYNC_ON, idle, NULL);
  CHECK_EQ_U64(drain(&s, 0, size, 128 * 1024), size);
  CHECK(idle_calls > 0); /* the reads really ran in the background */
  source_async(&s, SRC_ASYNC_OFF, NULL, NULL);
  CHECK_EQ_INT(fake_fxio_violations, 0);
  source_close(&s);
}

TEST(udpfs_readahead_odd_blocks_and_seek) {
  static GameSource s;
  static udpfs_src_t u;
  uint64_t size = 3 * 1024 * 1024;
  open_udpfs(&s, &u, size);
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  CHECK_EQ_U64(drain(&s, 0, 700 * 1024, 300 * 1024), 700 * 1024);
  /* Seek back (resume) and forward: still the right bytes. */
  CHECK(s.ops->seek(&s, 64 * 2048, SRC_SEEK_SET) == 64 * 2048);
  CHECK_EQ_U64(drain(&s, 64 * 2048, 1024 * 1024, 64 * 1024), 1024 * 1024 - 64 * 2048);
  CHECK(s.ops->seek(&s, 2 * 1024 * 1024, SRC_SEEK_SET) == 2 * 1024 * 1024);
  CHECK_EQ_U64(drain(&s, 2 * 1024 * 1024, size, 128 * 1024), 1024 * 1024);
  source_async(&s, SRC_ASYNC_OFF, NULL, NULL);
  CHECK_EQ_INT(fake_fxio_violations, 0);
  source_close(&s);
}

TEST(udpfs_sync_lets_the_caller_use_filexio) {
  static GameSource s;
  static udpfs_src_t u;
  uint64_t size = 4 * 1024 * 1024;
  open_udpfs(&s, &u, size);
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  CHECK_EQ_U64(drain(&s, 0, 1024 * 1024, 128 * 1024), 1024 * 1024);
  source_async(&s, SRC_ASYNC_SYNC, NULL, NULL);
  /* A checkpoint writes the journal through fileXio here. */
  fileXioLseek64(9, 0, 0);
  CHECK_EQ_INT(fake_fxio_violations, 0);
  CHECK_EQ_U64(drain(&s, 1024 * 1024, size, 128 * 1024), 3 * 1024 * 1024);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
}

TEST(udpfs_readahead_error_then_retry) {
  static GameSource s;
  static udpfs_src_t u;
  uint64_t size = 3 * 1024 * 1024;
  open_udpfs(&s, &u, size);
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  fake_fxio_fail_read = 3; /* a read in the middle fails once */
  static uint8_t buf[1 << 20];
  uint64_t pos = 0;
  int errors = 0;
  while (pos < size && errors < 5) {
    const uint8_t *p;
    uint32_t got;
    if (source_next_block(&s, buf, 128 * 1024, &p, &got) != ERR_OK) {
      errors++;
      continue; /* the next call fetches the same position again */
    }
    int ok = 1;
    for (uint32_t i = 0; i < got; i++)
      ok &= p[i] == fake_byte(pos + i);
    CHECK(ok);
    pos += got;
  }
  CHECK_EQ_INT(errors, 1);
  CHECK_EQ_U64(pos, size);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
}

TEST(udpfs_short_read_never_skips_bytes) {
  static GameSource s;
  static udpfs_src_t u;
  uint64_t size = 2 * 1024 * 1024;
  open_udpfs(&s, &u, size);
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  fake_fxio_short_read = 2; /* a mid-transfer failure reported as short */
  CHECK_EQ_U64(drain(&s, 0, size, 128 * 1024), size);
  source_close(&s);
  /* Without read-ahead too. */
  open_udpfs(&s, &u, size);
  fake_fxio_short_read = 2;
  static uint8_t buf[200000];
  CHECK_EQ_INT(source_read_exact(&s, buf, sizeof(buf)), ERR_OK);
  int ok = 1;
  for (uint32_t i = 0; i < sizeof(buf); i++)
    ok &= buf[i] == fake_byte(i);
  CHECK(ok);
  source_close(&s);
}

TEST(udpfs_plain_reads_without_async) {
  static GameSource s;
  static udpfs_src_t u;
  uint64_t size = 1024 * 1024 + 2048;
  open_udpfs(&s, &u, size);
  CHECK_EQ_U64(drain(&s, 0, size, 256 * 1024), size);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
}

/* ---- LZ4 frames ---- */

static void open_wire(GameSource *s, wire_src_t *w, uint64_t size, uint32_t req) {
  fake_fxio_reset(size, 1);
  source_wire_set_request(req);
  source_wire_init(s, w);
  CHECK_EQ_INT(source_open(s, "udpfs:/DVD/x.iso"), ERR_OK);
}

TEST(wire_source_exact_bytes_async_and_not) {
  static GameSource s;
  static wire_src_t w;
  uint64_t size = 3 * 1024 * 1024 + 2048 * 5;
  open_wire(&s, &w, size, 128 * 1024);
  CHECK_EQ_U64(source_size(&s), size);
  source_async(&s, SRC_ASYNC_ON, idle, NULL);
  CHECK_EQ_U64(drain(&s, 0, size, 128 * 1024), size);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
  open_wire(&s, &w, size, 64 * 1024);
  CHECK_EQ_U64(drain(&s, 0, size, 100 * 1024), size);
  source_close(&s);
}

TEST(wire_source_small_frames_and_seek) {
  static GameSource s;
  static wire_src_t w;
  uint64_t size = 2 * 1024 * 1024;
  open_wire(&s, &w, size, 128 * 1024);
  fake_fxio_max_frame = 6 * 2048; /* the server packs less per frame */
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  CHECK_EQ_U64(drain(&s, 0, 512 * 1024, 128 * 1024), 512 * 1024);
  CHECK(s.ops->seek(&s, 100 * 2048, SRC_SEEK_SET) == 100 * 2048);
  CHECK_EQ_U64(drain(&s, 100 * 2048, size, 128 * 1024), size - 100 * 2048);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
}

TEST(wire_source_lz4_frames_exact_bytes) {
  static GameSource s;
  static wire_src_t w;
  uint64_t size = 2 * 1024 * 1024 + 2048 * 3;
  open_wire(&s, &w, size, 128 * 1024);
  fake_fxio_wire_lz4 = 1;
  fake_fxio_max_frame = 40 * 2048; /* leaves room for the LZ4 overhead */
  source_async(&s, SRC_ASYNC_ON, idle, NULL);
  CHECK_EQ_U64(drain(&s, 0, size, 128 * 1024), size);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
}

TEST(wire_source_cut_frame_is_refetched_not_used) {
  static GameSource s;
  static wire_src_t w;
  uint64_t size = 1024 * 1024;
  open_wire(&s, &w, size, 128 * 1024);
  fake_fxio_wire_lz4 = 1;
  fake_fxio_max_frame = 40 * 2048;
  fake_fxio_short_read = 3; /* the third transfer arrives cut in half */
  source_async(&s, SRC_ASYNC_ON, NULL, NULL);
  static uint8_t buf[1 << 17];
  uint64_t pos = 0;
  int errors = 0, ok = 1;
  while (pos < size && errors < 5) {
    const uint8_t *p;
    uint32_t got;
    if (source_next_block(&s, buf, 64 * 1024, &p, &got) != ERR_OK) {
      errors++;
      continue;
    }
    for (uint32_t i = 0; i < got; i++)
      ok &= p[i] == fake_byte(pos + i);
    pos += got;
  }
  CHECK(ok);
  CHECK_EQ_INT(errors, 1);
  CHECK_EQ_U64(pos, size);
  source_close(&s);
  CHECK_EQ_INT(fake_fxio_violations, 0);
}

TEST(wire_source_failed_read_is_an_error_not_data) {
  static GameSource s;
  static wire_src_t w;
  uint64_t size = 1024 * 1024;
  open_wire(&s, &w, size, 128 * 1024);
  fake_fxio_fail_read = 2;
  static uint8_t buf[1 << 17];
  const uint8_t *p;
  uint32_t got;
  uint64_t pos = 0;
  int errors = 0;
  while (pos < size && errors < 5) {
    if (source_next_block(&s, buf, 64 * 1024, &p, &got) != ERR_OK) {
      errors++;
      continue;
    }
    int ok = 1;
    for (uint32_t i = 0; i < got; i++)
      ok &= p[i] == fake_byte(pos + i);
    CHECK(ok);
    pos += got;
  }
  CHECK_EQ_INT(errors, 1);
  CHECK_EQ_U64(pos, size);
  source_close(&s);
}
