#ifndef PSXI_SPACE_H
#define PSXI_SPACE_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Hard limit for games and data on the DESR HDD: 128 GiB. Never more,
 * whatever the drive or its game area offers.
 *  - in total: all partitions except the APA system ones (__mbr, __net,
 *    __system, __sysconf, __common, ...) count; a new partition must fit
 *    in what is left;
 *  - by position: no partition segment may end beyond 128 GiB (LBA
 *    2^28, the end of 28-bit addressing); a partition the driver placed
 *    beyond it is removed again.
 * Pure: host-tested. */

#define SPACE_LIMIT_MB (128ull * 1024ull)
#define SPACE_LIMIT_SECTORS (128ull * 1024ull * 1024ull * 2ull) /* 0x10000000 */

typedef struct {
  const char *name;
  uint32_t type;  /* APA type; 0 = free space */
  uint32_t attr;  /* APA flags; 1 = sub-partition */
  uint32_t start; /* first sector */
  uint32_t size;  /* sectors (512 bytes) */
} space_part_t;

typedef struct {
  uint64_t games_mb;      /* game partitions: __./PP. game names (PS2, PS1, covers) */
  uint64_t data_mb;       /* everything that is not an APA system partition */
  uint64_t system_mb;     /* __mbr, __net, __system, __sysconf, __common, ... */
  uint64_t limit_left_mb; /* SPACE_LIMIT_MB - data_mb, at least 0 */
  int beyond_limit;       /* some segment already ends beyond 128 GiB */
} space_usage_t;

/* An APA system partition: "__" not followed by '.'. */
int space_is_system(const char *name);

void space_tally(const space_part_t *p, int n, space_usage_t *u);

/* Room for `add_mb` more: ERR_OK, ERR_NO_SPACE (the HDD has less than
 * that free) or ERR_DATA_LIMIT (it would pass 128 GiB of games/data). */
inst_err_t space_check(const space_usage_t *u, uint32_t add_mb, uint32_t hdd_free_mb);

/* Space usable for new games: min(HDD free, what the limit leaves). */
uint64_t space_usable_mb(const space_usage_t *u, uint32_t hdd_free_mb);

/* A segment [start, start + size) ends beyond 128 GiB. */
int space_segment_beyond(uint32_t start, uint32_t size);

/* Any segment (main or sub) of partition `name` ends beyond 128 GiB. */
int space_name_beyond(const space_part_t *p, int n, const char *name);

/* "Games 4.3 GiB, games+data 4.5 of 128 GiB, HDD free 5.0 GiB" */
void space_format(const space_usage_t *u, uint32_t hdd_free_mb, char *out, size_t outsz);

#endif
