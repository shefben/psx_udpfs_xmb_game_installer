#include <string.h>

#include "util.h"

size_t str_copy(char *dst, const char *src, size_t dstsz) {
  size_t n = strlen(src);
  if (dstsz == 0)
    return n;
  size_t c = n < dstsz - 1 ? n : dstsz - 1;
  memcpy(dst, src, c);
  dst[c] = 0;
  return n;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

int str_ends_with_ci(const char *s, const char *suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  if (m > n)
    return 0;
  s += n - m;
  for (size_t i = 0; i < m; i++)
    if (lower(s[i]) != lower(suffix[i]))
      return 0;
  return 1;
}

void str_rtrim(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                   s[n - 1] == '\n'))
    s[--n] = 0;
}

uint16_t get_u16le(const void *p) {
  const uint8_t *b = p;
  return (uint16_t)(b[0] | (b[1] << 8));
}

uint32_t get_u32le(const void *p) {
  const uint8_t *b = p;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

void put_u32le(void *p, uint32_t v) {
  uint8_t *b = p;
  b[0] = (uint8_t)v;
  b[1] = (uint8_t)(v >> 8);
  b[2] = (uint8_t)(v >> 16);
  b[3] = (uint8_t)(v >> 24);
}

int parse_u64(const char *s, uint64_t *out) {
  uint64_t v = 0;
  if (!s || !*s)
    return -1;
  for (; *s; s++) {
    if (*s < '0' || *s > '9')
      return -1;
    uint64_t d = (uint64_t)(*s - '0');
    if (v > (UINT64_MAX - d) / 10)
      return -1;
    v = v * 10 + d;
  }
  *out = v;
  return 0;
}
