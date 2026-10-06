#ifndef PSXI_SPACE_H
#define PSXI_SPACE_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Safe limit for games and data on the DESR HDD: 128 GiB, the end of
 * 28-bit LBA addressing (LBA 2^28). Passing it is allowed only after a
 * firm warning the user confirms (space_gate_allow): past it, software
 * that still uses 28-bit commands on the DVRP reads the wrong sectors or
 * writes over the start of the HDD. Only a DESR with LBA48-aware custom
 * DVRP firmware (dvrpwned) and an enlarged PS2 area can use more.
 *  - in total: all partitions except the APA system ones (__mbr, __net,
 *    __system, __sysconf, __common, ...) count;
 *  - by position: a partition segment ending beyond LBA 2^28.
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

/* LBA48-aware DVRP firmware on this console (space_lba48_signature). */
typedef enum { SPACE_LBA48_UNKNOWN = 0, SPACE_LBA48_NO, SPACE_LBA48_YES } space_lba48_t;

/* The user's answer to the warning, kept until the installer restarts. */
typedef struct {
  int accepted;
} space_gate_t;

/* An APA system partition: "__" not followed by '.'. */
int space_is_system(const char *name);

void space_tally(const space_part_t *p, int n, space_usage_t *u);

/* Room for `add_mb` more: ERR_OK, ERR_NO_SPACE (the HDD has less than
 * that free; checked first) or ERR_DATA_LIMIT (it would pass 128 GiB of
 * games/data: allowed only through space_gate_allow). */
inst_err_t space_check(const space_usage_t *u, uint32_t add_mb, uint32_t hdd_free_mb);

/* A segment [start, start + size) ends beyond 128 GiB. */
int space_segment_beyond(uint32_t start, uint32_t size);

/* Any segment (main or sub) of partition `name` ends beyond 128 GiB. */
int space_name_beyond(const space_part_t *p, int n, const char *name);

/* Past the limit? 1 if the user accepted the warning this session or
 * accepts it now (prompt returns non-zero); 0 if declined or there is
 * no prompt (no UI: never past the limit). */
int space_gate_allow(space_gate_t *g, int (*prompt)(void *ctx), void *ctx);

/* The warning: the risk, games+data after adding `add_mb`, firmware. */
void space_warning_text(const space_usage_t *u, uint32_t add_mb, space_lba48_t lba48, char *out,
                        size_t outsz);

/* dvrpwned's "PS2LBA48" signature in ATA IDENTIFY words 121-124. */
int space_lba48_signature(const uint16_t identify[256]);

/* "Games 4.3 GiB, games+data 4.5 of 128 GiB, HDD free 5.0 GiB" */
void space_format(const space_usage_t *u, uint32_t hdd_free_mb, char *out, size_t outsz);

#endif
