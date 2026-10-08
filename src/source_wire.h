#ifndef PSXI_SOURCE_WIRE_H
#define PSXI_SOURCE_WIRE_H

#include "source.h"

/* GameSource for udpfsd's LZ4 wire frames (wire_frame.h): opens
 * "udpfs:" WIRE_PATH_PREFIX "<path>" and returns the same raw bytes as
 * the plain path, decompressed on the EE. Each read asks for one frame
 * of WIRE_REQ bytes; with set_async on, the next frame is fetched while
 * the caller works on the current one. Stored frames are lent straight
 * from the network buffer. */

#define WIRE_REQ_MAX (128 * 1024)

typedef struct {
  int fd;
  int64_t pos;     /* raw position of the next byte the caller gets */
  int64_t size;    /* raw size */
  int64_t srv_pos; /* raw position the server will continue at, -1 unknown */
  int async;
  void (*idle)(void *ctx);
  void *idle_ctx;
} wire_src_t;

/* Bytes per frame request: at most WIRE_REQ_MAX and fileXio's IOP buffer
 * (one udpfs read, so the server packs exactly one frame). Call after
 * the network is up (iop_status rw_buffer). */
void source_wire_set_request(uint32_t bytes);

void source_wire_init(GameSource *src, wire_src_t *w);

#endif
