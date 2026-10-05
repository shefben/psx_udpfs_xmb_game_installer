#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resume_seg.h"

int seg_parse(const char *text, seg_list_t *l) {
  memset(l, 0, sizeof(*l));
  for (const char *p = text; p && *p && l->n < SEG_MAX;) {
    char *end;
    unsigned long long b = strtoull(p, &end, 10);
    if (end == p || *end != ' ')
      break;
    p = end + 1;
    unsigned long c = strtoul(p, &end, 16);
    if (end == p || *end != ' ')
      break;
    p = end + 1;
    unsigned long s = strtoul(p, &end, 16);
    if (end == p || (*end != '\n' && *end != '\r' && *end))
      break;
    if (l->n && b <= l->s[l->n - 1].bytes)
      break;
    l->s[l->n].bytes = b;
    l->s[l->n].cum_crc = (uint32_t)c;
    l->s[l->n].seg_crc = (uint32_t)s;
    l->n++;
    p = end + strspn(end, "\r\n");
  }
  return l->n;
}

size_t seg_serialize(const seg_list_t *l, char *out, size_t outsz) {
  size_t off = 0;
  if (outsz)
    out[0] = 0;
  for (int i = 0; i < l->n; i++) {
    int r = snprintf(out + off, outsz - off, "%llu %08lx %08lx\n",
                     (unsigned long long)l->s[i].bytes, (unsigned long)l->s[i].cum_crc,
                     (unsigned long)l->s[i].seg_crc);
    if (r < 0 || (size_t)r >= outsz - off)
      return 0;
    off += (size_t)r;
  }
  return off;
}

int seg_add(seg_list_t *l, uint64_t bytes, uint32_t cum_crc, uint32_t seg_crc) {
  if (l->n && bytes <= l->s[l->n - 1].bytes)
    return -1;
  if (l->n == SEG_MAX) {
    /* Forget the oldest checkpoint; its successor's segment then also
     * covers it, so it can no longer be re-checked on its own. A resume
     * that reaches it starts over from 0, which is always correct. */
    memmove(l->s, l->s + 1, sizeof(l->s[0]) * (SEG_MAX - 1));
    l->n--;
    l->s[0].seg_crc = ~l->s[0].seg_crc; /* never matches: stop here */
  }
  l->s[l->n].bytes = bytes;
  l->s[l->n].cum_crc = cum_crc;
  l->s[l->n].seg_crc = seg_crc;
  l->n++;
  return 0;
}

uint64_t seg_start(const seg_list_t *l, int i) { return i > 0 ? l->s[i - 1].bytes : 0; }

int seg_pick_resume(const seg_list_t *l, int max_checks,
                    int (*read_crc)(void *ctx, uint64_t start, uint64_t end, uint32_t *crc),
                    void *ctx, int *checked) {
  *checked = 0;
  for (int i = l->n - 1; i >= 0 && *checked < max_checks; i--) {
    uint32_t crc = 0;
    (*checked)++;
    if (read_crc(ctx, seg_start(l, i), l->s[i].bytes, &crc) == 0 && crc == l->s[i].seg_crc)
      return i;
  }
  return -1;
}
