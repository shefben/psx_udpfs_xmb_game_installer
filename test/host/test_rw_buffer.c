#include "../../src/rw_buffer.h"
#include "test.h"

static int calls[8], ncalls, max_ok;

static int fake_set(int size) {
  if (ncalls < 8)
    calls[ncalls++] = size;
  return size <= max_ok ? 0 : -12; /* -ENOMEM: IOP heap too small */
}

TEST(rw_buffer_uses_one_udpfs_request_per_iop_call) {
  ncalls = 0;
  max_ok = 1 << 20;
  CHECK_EQ_INT(rw_buffer_setup(fake_set), RW_BUFFER_FAST);
  CHECK_EQ_INT(ncalls, 1);
  CHECK_EQ_INT(RW_BUFFER_FAST, 128 * 1024); /* = patched UDPFS_MAX_READ */
}

TEST(rw_buffer_falls_back_to_64k_then_fileXio_default) {
  /* A failed SetRWBufferSize leaves fileXio with no buffer at all, so a
   * smaller size must be set again before any read or write. */
  ncalls = 0;
  max_ok = 64 * 1024;
  CHECK_EQ_INT(rw_buffer_setup(fake_set), RW_BUFFER_MID);
  CHECK_EQ_INT(ncalls, 2);
  CHECK_EQ_INT(calls[1], 64 * 1024);
  ncalls = 0;
  max_ok = 16 * 1024;
  CHECK_EQ_INT(rw_buffer_setup(fake_set), RW_BUFFER_DEFAULT);
  CHECK_EQ_INT(ncalls, 3);
  CHECK_EQ_INT(calls[2], 16 * 1024);
}

TEST(rw_buffer_reports_no_buffer) {
  ncalls = 0;
  max_ok = 0;
  CHECK_EQ_INT(rw_buffer_setup(fake_set), 0);
}
