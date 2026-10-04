#ifndef PSXI_HDL_PLAN_H
#define PSXI_HDL_PLAN_H

#include <stdint.h>

#include "errors.h"

/* APA sizing for an HDL game partition (plan section 12). Pure.
 *
 * Reserves are exactly what hdlfs_format() (HDLGameInstaller hdlfs/
 * main.c) uses when it fills the partitions: 0x2000 HDD sectors (4 MiB,
 * = 2048 CD sectors) in the main partition, 4 HDD sectors (2 KiB, = 1
 * CD sector) in each sub-partition. hdlfs fills partitions in order,
 * each as far as it goes, and rejects a slice of 0x200000 CD sectors
 * (4 GiB) or more. */

#define HDL_MAX_SUBS 64 /* APA_MAXSUB */
#define HDL_MAIN_RESERVE_2K 2048u
#define HDL_SUB_RESERVE_2K 1u
#define HDL_MAX_PART_MB 4096u /* largest APA bucket; HDLFS slice limit */

typedef struct {
  uint32_t main_mb;
  const char *main_size_str; /* "128M" ... "4G" */
  int subs;
  uint32_t sub_mb[HDL_MAX_SUBS];
  const char *sub_size_str[HDL_MAX_SUBS];
  uint32_t total_mb; /* main + all subs */
} hdl_alloc_t;

/* Plan partitions for `data_bytes` (a multiple of 2048) using only APA
 * buckets <= `max_part_mb` (the drive's HDIOC_MAXSECTOR limit; values
 * above 4096 are clamped). Fewest partitions; each partition the
 * smallest bucket that holds what remains, else the largest allowed.
 * ERR_INVALID_ARG for zero/unaligned sizes, ERR_HDL_PLAN if no bucket
 * is allowed or more than HDL_MAX_SUBS sub-partitions would be needed. */
inst_err_t hdl_plan_alloc(uint64_t data_bytes, uint32_t max_part_mb,
                          hdl_alloc_t *out);

/* Usable data capacity of an allocation, in bytes (hdlfs reserves). */
uint64_t hdl_alloc_capacity(const hdl_alloc_t *a);

/* Simulate hdlfs_format's fill: CD sectors per partition (main first).
 * Returns the number of partitions. */
int hdl_plan_fill(const hdl_alloc_t *a, uint64_t data_bytes,
                  uint32_t sectors[HDL_MAX_SUBS + 1]);

/* Map a size in MB to the accepted APA size string, NULL if none. */
const char *apa_size_str(uint32_t mb);

#endif
