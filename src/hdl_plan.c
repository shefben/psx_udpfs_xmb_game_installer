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
#define S2K_PER_MB 512u

const char *apa_size_str(uint32_t mb) {
  for (unsigned i = 0; i < NBUCKETS; i++)
    if (BUCKETS[i].mb == mb)
      return BUCKETS[i].str;
  return NULL;
}

/* Usable CD sectors in a partition of `mb` with `reserve` CD sectors. */
static uint64_t usable(uint32_t mb, uint32_t reserve) {
  return (uint64_t)mb * S2K_PER_MB - reserve;
}

/* Smallest allowed bucket holding `need` CD sectors, else the largest
 * allowed one. `n_allowed` buckets (BUCKETS[0..n_allowed-1]) are allowed. */
static unsigned pick(uint64_t need, uint32_t reserve, unsigned n_allowed) {
  for (unsigned i = 0; i < n_allowed; i++)
    if (usable(BUCKETS[i].mb, reserve) >= need)
      return i;
  return n_allowed - 1;
}

inst_err_t hdl_plan_alloc(uint64_t data_bytes, uint32_t max_part_mb,
                          hdl_alloc_t *out) {
  memset(out, 0, sizeof(*out));
  if (data_bytes == 0 || data_bytes % 2048)
    return ERR_INVALID_ARG;
  if (max_part_mb > HDL_MAX_PART_MB)
    max_part_mb = HDL_MAX_PART_MB;
  unsigned allowed = 0;
  while (allowed < NBUCKETS && BUCKETS[allowed].mb <= max_part_mb)
    allowed++;
  if (allowed == 0)
    return ERR_HDL_PLAN;

  uint64_t remaining = data_bytes / 2048;
  unsigned b = pick(remaining, HDL_MAIN_RESERVE_2K, allowed);
  out->main_mb = BUCKETS[b].mb;
  out->main_size_str = BUCKETS[b].str;
  out->total_mb = out->main_mb;
  uint64_t cap = usable(out->main_mb, HDL_MAIN_RESERVE_2K);
  remaining = remaining > cap ? remaining - cap : 0;

  while (remaining > 0) {
    if (out->subs >= HDL_MAX_SUBS) {
      memset(out, 0, sizeof(*out));
      return ERR_HDL_PLAN;
    }
    b = pick(remaining, HDL_SUB_RESERVE_2K, allowed);
    out->sub_mb[out->subs] = BUCKETS[b].mb;
    out->sub_size_str[out->subs] = BUCKETS[b].str;
    out->subs++;
    out->total_mb += BUCKETS[b].mb;
    cap = usable(BUCKETS[b].mb, HDL_SUB_RESERVE_2K);
    remaining = remaining > cap ? remaining - cap : 0;
  }
  return ERR_OK;
}

uint64_t hdl_alloc_capacity(const hdl_alloc_t *a) {
  uint64_t s = usable(a->main_mb, HDL_MAIN_RESERVE_2K);
  for (int i = 0; i < a->subs; i++)
    s += usable(a->sub_mb[i], HDL_SUB_RESERVE_2K);
  return s * 2048;
}

int hdl_plan_fill(const hdl_alloc_t *a, uint64_t data_bytes,
                  uint32_t sectors[HDL_MAX_SUBS + 1]) {
  uint64_t remaining = data_bytes / 2048;
  for (int i = 0; i <= a->subs; i++) {
    uint64_t cap = i == 0 ? usable(a->main_mb, HDL_MAIN_RESERVE_2K)
                          : usable(a->sub_mb[i - 1], HDL_SUB_RESERVE_2K);
    uint64_t take = remaining < cap ? remaining : cap;
    sectors[i] = (uint32_t)take;
    remaining -= take;
  }
  return 1 + a->subs;
}
