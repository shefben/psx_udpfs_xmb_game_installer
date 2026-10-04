#include <string.h>

#include "hdl_plan.h"

/* The APA driver only accepts these size strings (ps2-usbhdl iso.c). */
static const struct {
  const char *str;
  uint32_t mb;
} BUCKETS[] = {
    {"128M", 128}, {"256M", 256}, {"512M", 512},
    {"1G", 1024},  {"2G", 2048},  {"4G", 4096},
};
#define NBUCKETS (sizeof(BUCKETS) / sizeof(BUCKETS[0]))
#define MB (1024ull * 1024ull)

const char *apa_size_str(uint32_t mb) {
  for (unsigned i = 0; i < NBUCKETS; i++)
    if (BUCKETS[i].mb == mb)
      return BUCKETS[i].str;
  return NULL;
}

/* Smallest bucket with at least `need_mb`; the largest if none. */
static unsigned pick(uint64_t need_mb) {
  for (unsigned i = 0; i < NBUCKETS; i++)
    if (BUCKETS[i].mb >= need_mb)
      return i;
  return NBUCKETS - 1;
}

static uint64_t ceil_mb(uint64_t bytes) { return (bytes + MB - 1) / MB; }

inst_err_t hdl_plan_alloc(uint64_t data_bytes, hdl_alloc_t *out) {
  memset(out, 0, sizeof(*out));
  if (data_bytes == 0)
    return ERR_INVALID_ARG;

  uint64_t need = ceil_mb(data_bytes);
  unsigned b = pick(need + HDL_MAIN_RESERVE_MB);
  out->main_mb = BUCKETS[b].mb;
  out->main_size_str = BUCKETS[b].str;
  out->total_mb = out->main_mb;

  uint64_t main_data = out->main_mb - HDL_MAIN_RESERVE_MB;
  uint64_t remaining = need > main_data ? need - main_data : 0;
  while (remaining > 0) {
    if (out->subs >= HDL_MAX_SUBS)
      return ERR_INVALID_ARG;
    unsigned sb = pick(remaining + HDL_SUB_RESERVE_MB);
    uint64_t data = BUCKETS[sb].mb - HDL_SUB_RESERVE_MB;
    out->sub_mb[out->subs] = BUCKETS[sb].mb;
    out->sub_size_str[out->subs] = BUCKETS[sb].str;
    out->subs++;
    out->total_mb += BUCKETS[sb].mb;
    remaining = remaining > data ? remaining - data : 0;
  }
  return ERR_OK;
}

uint64_t hdl_alloc_capacity(const hdl_alloc_t *a) {
  uint64_t mb = a->main_mb - HDL_MAIN_RESERVE_MB;
  for (int i = 0; i < a->subs; i++)
    mb += a->sub_mb[i] - HDL_SUB_RESERVE_MB;
  return mb * MB;
}
