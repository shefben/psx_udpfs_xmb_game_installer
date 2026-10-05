#ifndef PSXI_RESUME_SEG_H
#define PSXI_RESUME_SEG_H

#include <stddef.h>
#include <stdint.h>

/* Copy checkpoints of one install, kept next to its journal
 * (install-<name>.seg): for each checkpoint the bytes on the HDD so far,
 * the CRC-32 of all source bytes so far (to continue the stream CRC) and
 * the CRC-32 of just this segment (to re-check it from the HDD).
 * Text, one checkpoint per line: "<bytes> <cum crc> <segment crc>". */

#define SEG_MAX 256

typedef struct {
  uint64_t bytes;   /* end of this segment = bytes on the HDD */
  uint32_t cum_crc; /* CRC-32 of source bytes [0, bytes) */
  uint32_t seg_crc; /* CRC-32 of source bytes [previous end, bytes) */
} seg_t;

typedef struct {
  int n;
  seg_t s[SEG_MAX];
} seg_list_t;

/* Parse; malformed lines and lines that do not move forward are dropped
 * (and everything after them). Returns the count. */
int seg_parse(const char *text, seg_list_t *l);
size_t seg_serialize(const seg_list_t *l, char *out, size_t outsz);

/* Add a checkpoint at `bytes` (must be past the last one; a full list
 * merges its two oldest entries' info by dropping the oldest). 0 / -1. */
int seg_add(seg_list_t *l, uint64_t bytes, uint32_t cum_crc, uint32_t seg_crc);

/* Start of segment i. */
uint64_t seg_start(const seg_list_t *l, int i);

/* Where to resume: check segments from the newest back (read_crc reads
 * [start, end) from the HDD and returns its CRC-32 in *crc, 0 / <0), and
 * return the index of the newest segment that reads back correctly, or
 * -1 if none does (resume from the very start). At most max_checks
 * segments are read; *checked counts them. The final full read-back of
 * the install still verifies everything before the resume point. */
int seg_pick_resume(const seg_list_t *l, int max_checks,
                    int (*read_crc)(void *ctx, uint64_t start, uint64_t end, uint32_t *crc),
                    void *ctx, int *checked);

#endif
