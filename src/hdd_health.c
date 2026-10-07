#include <string.h>

#include "hdd_health.h"
#include "hdl_plan.h"

int smart_parse(const uint8_t data[512], smart_attr_t *out, int max) {
  uint8_t sum = 0;
  int any = 0;
  for (int i = 0; i < 512; i++) {
    sum = (uint8_t)(sum + data[i]);
    any |= data[i];
  }
  if (sum != 0 || !any)
    return -1;
  int n = 0;
  for (int i = 0; i < SMART_MAX_ATTR && n < max; i++) {
    const uint8_t *e = data + 2 + 12 * i;
    if (e[0] == 0)
      continue;
    out[n].id = e[0];
    out[n].value = e[3];
    out[n].worst = e[4];
    uint64_t raw = 0;
    for (int b = 5; b >= 0; b--)
      raw = raw << 8 | e[5 + b];
    out[n].raw = raw;
    n++;
  }
  return n;
}

const char *smart_attr_name(uint8_t id) {
  switch (id) {
  case 1:
    return "Read error rate";
  case 5:
    return "Reallocated sectors";
  case 9:
    return "Power-on hours";
  case 10:
    return "Spin-up retries";
  case 12:
    return "Power cycles";
  case 187:
    return "Reported uncorrectable";
  case 190:
  case 194:
    return "Temperature (C)";
  case 196:
    return "Reallocation events";
  case 197:
    return "Pending sectors";
  case 198:
    return "Offline uncorrectable";
  case 199:
    return "UDMA CRC errors";
  default:
    return NULL;
  }
}

uint64_t smart_attr_display(const smart_attr_t *a) {
  if (a->id == 194 || a->id == 190)
    return a->raw & 0xFF;
  if (a->id == 9)
    return a->raw & 0xFFFFFFFFu;
  return a->raw;
}

health_t smart_verdict(int status, const smart_attr_t *a, int n) {
  if (status == 1)
    return HEALTH_FAILING;
  if (status < 0 && n <= 0)
    return HEALTH_UNKNOWN;
  for (int i = 0; i < n; i++) {
    uint64_t raw = a[i].raw & 0xFFFFFFFFu;
    if ((a[i].id == 5 || a[i].id == 187 || a[i].id == 197 || a[i].id == 198) && raw > 0)
      return HEALTH_WATCH;
  }
  return HEALTH_GOOD;
}

const char *health_label(health_t h) {
  switch (h) {
  case HEALTH_GOOD:
    return "GOOD";
  case HEALTH_WATCH:
    return "WATCH (bad sectors reported: back up and keep an eye on it)";
  case HEALTH_FAILING:
    return "FAILING (SMART threshold exceeded: replace the drive)";
  default:
    return "UNKNOWN (no SMART data through this drive)";
  }
}

static int fits(uint64_t bytes, uint32_t budget_mb, uint32_t max_part_mb) {
  hdl_alloc_t a;
  return hdl_plan_alloc(bytes, max_part_mb, &a) == ERR_OK && a.total_mb <= budget_mb;
}

uint64_t hdd_largest_game(uint32_t budget_mb, uint32_t max_part_mb) {
  const uint64_t MB = 1024 * 1024;
  uint64_t lo = 0, hi = (uint64_t)budget_mb + 1; /* in MiB: lo fits, hi does not */
  if (!fits(MB, budget_mb, max_part_mb))
    return 0;
  lo = 1;
  while (hi - lo > 1) {
    uint64_t mid = lo + (hi - lo) / 2;
    if (fits(mid * MB, budget_mb, max_part_mb))
      lo = mid;
    else
      hi = mid;
  }
  return lo * MB;
}
