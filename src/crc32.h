#ifndef PSXI_CRC32_H
#define PSXI_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32 (IEEE 802.3, zlib-compatible). Start with crc = 0 and feed the
 * running value back in: crc32_update(crc32_update(0, a, n), b, m) is
 * the CRC of a||b. Used for accidental-corruption detection of the
 * copied game data, not as a security measure. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

#endif
