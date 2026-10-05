#include <string.h>

#include "crc32.h"

/* Slice-by-8: eight table lookups per 8 input bytes instead of one per
 * byte. Same polynomial and results as the bytewise loop. */
static uint32_t table[8][256];
static int table_ready;

static void make_table(void) {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    table[0][i] = c;
  }
  for (uint32_t i = 0; i < 256; i++)
    for (int s = 1; s < 8; s++)
      table[s][i] = table[0][table[s - 1][i] & 0xFF] ^ (table[s - 1][i] >> 8);
  table_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len) {
  const uint8_t *p = data;
  if (!table_ready)
    make_table();
  crc = ~crc;
  while (len && ((uintptr_t)p & 3)) {
    crc = table[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    len--;
  }
  /* Little-endian 32-bit loads (EE and x86 hosts). */
  while (len >= 8) {
    uint32_t a, b; /* memcpy: no aliasing issue; aligned, so one load each */
    const uint8_t *q = __builtin_assume_aligned(p, 4);
    memcpy(&a, q, 4);
    memcpy(&b, q + 4, 4);
    a ^= crc;
    crc = table[7][a & 0xFF] ^ table[6][(a >> 8) & 0xFF] ^ table[5][(a >> 16) & 0xFF] ^
          table[4][a >> 24] ^ table[3][b & 0xFF] ^ table[2][(b >> 8) & 0xFF] ^
          table[1][(b >> 16) & 0xFF] ^ table[0][b >> 24];
    p += 8;
    len -= 8;
  }
  while (len--)
    crc = table[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
