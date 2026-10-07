#ifndef PSXI_HDD_HEALTH_H
#define PSXI_HDD_HEALTH_H

#include <stddef.h>
#include <stdint.h>

/* HDD health (pure): SMART data and what still fits. */

#define SMART_MAX_ATTR 30

typedef struct {
  uint8_t id;
  uint8_t value, worst; /* normalised, 1..253 (higher is better) */
  uint64_t raw;         /* 48-bit raw value */
} smart_attr_t;

/* Attributes of a SMART READ DATA block (512 bytes): checks its checksum
 * (all bytes sum to 0). Returns the number of attributes, -1 if invalid. */
int smart_parse(const uint8_t data[512], smart_attr_t *out, int max);

/* Name of a well-known attribute, or NULL. */
const char *smart_attr_name(uint8_t id);

/* Value to show: temperature in C for 194/190, hours for 9, else raw. */
uint64_t smart_attr_display(const smart_attr_t *a);

typedef enum {
  HEALTH_UNKNOWN = 0, /* no SMART through this drive/DVRP */
  HEALTH_GOOD,
  HEALTH_WATCH,   /* reallocated / pending / uncorrectable sectors */
  HEALTH_FAILING, /* SMART RETURN STATUS: threshold exceeded */
} health_t;

/* status: SMART RETURN STATUS (0 ok, 1 exceeded, <0 unavailable). */
health_t smart_verdict(int status, const smart_attr_t *a, int n);
const char *health_label(health_t h);

/* Largest game (bytes, multiple of 1 MiB) whose HDL partitions need at
 * most budget_mb with partitions up to max_part_mb (hdl_plan_alloc). 0 if
 * not even a small one fits. */
uint64_t hdd_largest_game(uint32_t budget_mb, uint32_t max_part_mb);

#endif
