#include <stdlib.h>

#include "../../src/crc32.h"
#include "../../src/sha256.h"
#include "test.h"

TEST(crc32_standard_vectors) {
  CHECK_EQ_INT(crc32_update(0, "", 0), 0);
  CHECK_EQ_INT(crc32_update(0, "123456789", 9), 0xCBF43926u);
  CHECK_EQ_INT(crc32_update(0, "The quick brown fox jumps over the lazy dog", 43), 0x414FA339u);
}

TEST(crc32_incremental_equals_oneshot) {
  static uint8_t buf[100000];
  for (size_t i = 0; i < sizeof(buf); i++)
    buf[i] = (uint8_t)(i * 31 + (i >> 7));
  uint32_t one = crc32_update(0, buf, sizeof(buf));
  uint32_t inc = 0;
  for (size_t off = 0; off < sizeof(buf); off += 4093) {
    size_t n = sizeof(buf) - off < 4093 ? sizeof(buf) - off : 4093;
    inc = crc32_update(inc, buf + off, n);
  }
  CHECK_EQ_INT(one, inc);
}

static void hex(const char *msg, size_t len, char out[65]) {
  sha256_ctx c;
  uint8_t d[32];
  sha256_init(&c);
  sha256_update(&c, msg, len);
  sha256_final(&c, d);
  sha256_to_hex(d, out);
}

TEST(sha256_standard_vectors) {
  char h[65];
  hex("", 0, h);
  CHECK_STR(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  hex("abc", 3, h);
  CHECK_STR(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, h);
  CHECK_STR(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(sha256_million_a_incremental) {
  sha256_ctx c;
  uint8_t d[32];
  char h[65];
  char a[1000];
  memset(a, 'a', sizeof(a));
  sha256_init(&c);
  for (int i = 0; i < 1000; i++)
    sha256_update(&c, a, sizeof(a));
  sha256_final(&c, d);
  sha256_to_hex(d, h);
  CHECK_STR(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
