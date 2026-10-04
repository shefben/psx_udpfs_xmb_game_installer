#ifndef PSXI_UTIL_H
#define PSXI_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Bounded copy that always NUL-terminates (dstsz > 0). Returns the
 * length of src so callers can detect truncation. */
size_t str_copy(char *dst, const char *src, size_t dstsz);

/* 1 if `s` ends with `suffix`, ASCII case-insensitive. */
int str_ends_with_ci(const char *s, const char *suffix);

/* Remove trailing spaces/tabs/CR/LF in place. */
void str_rtrim(char *s);

uint16_t get_u16le(const void *p);
uint32_t get_u32le(const void *p);
void put_u32le(void *p, uint32_t v);

/* Parse an unsigned 64-bit decimal. Returns 0 on success, -1 on a
 * malformed or overflowing value. */
int parse_u64(const char *s, uint64_t *out);

#endif
