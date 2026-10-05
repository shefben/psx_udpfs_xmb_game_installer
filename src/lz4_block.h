#ifndef PSXI_LZ4_BLOCK_H
#define PSXI_LZ4_BLOCK_H

#include <stddef.h>
#include <stdint.h>

/* LZ4 raw block decompression (no frame), bounds-checked: used by the
 * ZSO reader. Returns the decompressed size, or -1 on malformed input or
 * if the output would exceed outsz. */
int lz4_block_decompress(const uint8_t *in, size_t inlen, uint8_t *out, size_t outsz);

#endif
