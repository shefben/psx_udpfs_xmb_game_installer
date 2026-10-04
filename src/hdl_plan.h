#ifndef PSXI_HDL_PLAN_H
#define PSXI_HDL_PLAN_H

#include <stdint.h>

#include "errors.h"

/* APA sizing for an HDL game partition (plan section 12). Pure. */

#define HDL_MAX_SUBS 8
#define HDL_MAIN_RESERVE_MB 4 /* 0x2000 sectors: PPAA/OSD + HDL header area */
#define HDL_SUB_RESERVE_MB 1  /* 0x0800 sectors per sub-partition */
#define HDL_MAX_PART_MB 4096  /* HDLFS slice size is a 32-bit byte count */

typedef struct {
  uint32_t main_mb;
  const char *main_size_str; /* "128M" ... "4G" */
  int subs;
  uint32_t sub_mb[HDL_MAX_SUBS];
  const char *sub_size_str[HDL_MAX_SUBS];
  uint32_t total_mb; /* main + all subs */
} hdl_alloc_t;

/* Smallest bucket allocation that holds `data_bytes` of disc data,
 * rounding *up* so no data byte is left without space. ERR_OK or
 * ERR_INVALID_ARG (zero size / more than HDL_MAX_SUBS needed). */
inst_err_t hdl_plan_alloc(uint64_t data_bytes, hdl_alloc_t *out);

/* Usable data capacity of an allocation, in bytes. */
uint64_t hdl_alloc_capacity(const hdl_alloc_t *a);

/* Map a size in MB to the accepted APA size string, NULL if none. */
const char *apa_size_str(uint32_t mb);

#endif
