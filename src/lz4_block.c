#include <string.h>

#include "lz4_block.h"

/* Length field: 4-bit nibble, 15 means more bytes follow (255 = more). */
static int read_len(const uint8_t **ip, const uint8_t *end, size_t *len) {
  if (*len != 15)
    return 0;
  for (;;) {
    if (*ip >= end)
      return -1;
    uint8_t b = *(*ip)++;
    *len += b;
    if (b != 255)
      return 0;
  }
}

int lz4_block_decompress(const uint8_t *in, size_t inlen, uint8_t *out, size_t outsz) {
  const uint8_t *ip = in, *iend = in + inlen;
  size_t op = 0;
  /* Stop once outsz bytes are out: ZSO pads aligned blocks after the
   * data, and the caller passes the exact block length. */
  while (ip < iend && op < outsz) {
    uint8_t token = *ip++;
    size_t lit = token >> 4;
    if (read_len(&ip, iend, &lit) < 0 || lit > (size_t)(iend - ip) || lit > outsz - op)
      return -1;
    memcpy(out + op, ip, lit);
    op += lit;
    ip += lit;
    if (ip == iend)
      break; /* the last sequence has literals only */
    if (iend - ip < 2)
      return -1;
    size_t off = (size_t)ip[0] | ((size_t)ip[1] << 8);
    ip += 2;
    size_t mlen = token & 15;
    if (read_len(&ip, iend, &mlen) < 0)
      return -1;
    mlen += 4;
    if (off == 0 || off > op || mlen > outsz - op)
      return -1;
    if (off >= mlen) {
      memcpy(out + op, out + op - off, mlen);
      op += mlen;
    } else {
      /* Byte by byte: the match overlaps its own output. */
      for (size_t i = 0; i < mlen; i++, op++)
        out[op] = out[op - off];
    }
  }
  return (int)op;
}
