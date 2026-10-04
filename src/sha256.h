#ifndef PSXI_SHA256_H
#define PSXI_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4). Used by the diagnostics page to print the hash
 * of embedded IRX/KELF payloads so they can be compared with the build
 * manifest. */
typedef struct {
  uint32_t h[8];
  uint64_t len;
  uint8_t buf[64];
  size_t used;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256_to_hex(const uint8_t d[32], char out[65]);

/* One-shot helper: lowercase hex of SHA-256(data). */
void sha256_hex(const void *data, size_t len, char out[65]);

#endif
