#include <stdio.h>
#include <string.h>

#include "partname.h"
#include "space.h"

int space_is_system(const char *name) {
  return name && name[0] == '_' && name[1] == '_' && name[2] != '.';
}

void space_tally(const space_part_t *p, int n, space_usage_t *u) {
  memset(u, 0, sizeof(*u));
  uint64_t games = 0, data = 0, sys = 0; /* in sectors */
  for (int i = 0; i < n; i++) {
    if (p[i].type == 0) /* free space */
      continue;
    if (space_segment_beyond(p[i].start, p[i].size))
      u->beyond_limit = 1;
    if (space_is_system(p[i].name)) {
      sys += p[i].size;
      continue;
    }
    data += p[i].size;
    if (partition_is_hidden_game(p[i].name) || partition_is_game_channel(p[i].name))
      games += p[i].size;
  }
  u->games_mb = games / 2048;
  u->data_mb = data / 2048;
  u->system_mb = sys / 2048;
  u->limit_left_mb = u->data_mb >= SPACE_LIMIT_MB ? 0 : SPACE_LIMIT_MB - u->data_mb;
}

inst_err_t space_check(const space_usage_t *u, uint32_t add_mb, uint32_t hdd_free_mb) {
  if ((uint64_t)add_mb > u->limit_left_mb)
    return ERR_DATA_LIMIT;
  if (add_mb > hdd_free_mb)
    return ERR_NO_SPACE;
  return ERR_OK;
}

uint64_t space_usable_mb(const space_usage_t *u, uint32_t hdd_free_mb) {
  return (uint64_t)hdd_free_mb < u->limit_left_mb ? hdd_free_mb : u->limit_left_mb;
}

int space_segment_beyond(uint32_t start, uint32_t size) {
  return (uint64_t)start + size > SPACE_LIMIT_SECTORS;
}

int space_name_beyond(const space_part_t *p, int n, const char *name) {
  for (int i = 0; i < n; i++)
    if (p[i].type != 0 && !strcmp(p[i].name, name) && space_segment_beyond(p[i].start, p[i].size))
      return 1;
  return 0;
}

/* MiB as GiB with one decimal, rounded half up (integer only). */
static void gib(uint64_t mb, char *out, size_t outsz) {
  uint64_t t = (mb * 10 + 512) / 1024;
  snprintf(out, outsz, "%llu.%llu", (unsigned long long)(t / 10), (unsigned long long)(t % 10));
}

void space_format(const space_usage_t *u, uint32_t hdd_free_mb, char *out, size_t outsz) {
  char g[24], d[24], f[24];
  gib(u->games_mb, g, sizeof(g));
  gib(u->data_mb, d, sizeof(d));
  gib(hdd_free_mb, f, sizeof(f));
  snprintf(out, outsz, "Games %s GiB, games+data %s of %llu GiB, HDD free %s GiB", g, d,
           (unsigned long long)(SPACE_LIMIT_MB / 1024), f);
}
