#ifndef PSXI_WIRE_FRAME_H
#define PSXI_WIRE_FRAME_H

#include <stddef.h>
#include <stdint.h>

/* LZ4 wire compression (pure). The patched udpfsd serves every image
 * also under WIRE_PATH_PREFIX + its path ("/.lz4f/DVD/x.iso"): there each
 * READ_REQ of N bytes returns whole frames of the next raw bytes, at most
 * wire_frame_raw_for(N) raw bytes each, and moves the file position by
 * the raw bytes; seeks and the file size are in raw bytes. A frame:
 *
 *   u32 magic    WIRE_MAGIC ("LZF1")
 *   u32 raw_len  raw bytes it carries
 *   u32 data_len bytes of data after this header
 *   u32 flags    WIRE_F_LZ4: data is one LZ4 block, else raw_len stored bytes
 *   data, zero-padded to a multiple of 16 (SIF DMA unit)
 *
 * All little-endian. udpfsd advertises it with "wire=lz4f" in the
 * manifest header. Same format in patches/udpfsd (wireframe.go). */

#define WIRE_MAGIC 0x31465A4Cu /* "LZF1" */
#define WIRE_HDR 16u
#define WIRE_F_LZ4 1u
#define WIRE_PATH_PREFIX "/.lz4f"

typedef struct {
  uint32_t raw_len;
  uint32_t data_len;
  uint32_t flags;
  uint32_t total; /* header + padded data: offset of the next frame */
  const uint8_t *data;
} wire_frame_t;

/* Raw bytes per frame the server packs into a read of `req` bytes
 * (2048-byte multiples so frames stay sector-aligned). */
uint32_t wire_frame_raw_for(uint32_t req);

/* Parse the frame at buf (avail bytes, as received). raw_max: largest
 * raw_len accepted. Returns its total size, or -1 if malformed. */
int wire_frame_parse(const uint8_t *buf, uint32_t avail, uint32_t raw_max, wire_frame_t *f);

/* Raw bytes of a parsed frame into out (raw_len bytes). 0, or -1. */
int wire_frame_decode(const wire_frame_t *f, uint8_t *out);

/* Host tests: a stored frame of `raw` into out (WIRE_HDR + raw_len + 16
 * bytes). Returns its total size. */
uint32_t wire_frame_build_stored(const uint8_t *raw, uint32_t raw_len, uint8_t *out);

/* "udpfs:/DVD/x.iso" -> "udpfs:/.lz4f/DVD/x.iso". 0, or -1 if not a
 * udpfs path or too long. */
int wire_path(const char *path, char *out, size_t outsz);

#endif
