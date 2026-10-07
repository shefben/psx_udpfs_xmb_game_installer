#ifndef PSXI_SOURCE_UDPFS_H
#define PSXI_SOURCE_UDPFS_H

#include "source.h"

/* GameSource over any fileXio device (udpfs:, mass0:). Opens exactly the
 * path given — for ZSO that is udpfsd's virtual "<name>.zso.iso", which
 * serves decompressed ISO bytes.
 *
 * With set_async on, it keeps one read of the next UDPFS_RA_SIZE bytes
 * in flight (fileXio's non-blocking mode) while the caller works on the
 * current block, and lends its buffer (read_ptr) instead of copying.
 * Only one source may have it on at a time (shared buffers). */

#define UDPFS_RA_SIZE (512 * 1024)

typedef struct {
  int fd;
  int64_t pos;     /* logical position: the next byte the caller gets */
  int64_t size;    /* cached logical size */
  int64_t srv_pos; /* where the device's own position is, -1 unknown */
  int async;
  void (*idle)(void *ctx);
  void *idle_ctx;
} udpfs_src_t;

void source_udpfs_init(GameSource *src, udpfs_src_t *u);

/* Shared by the fileXio sources: wait for this source's read in flight
 * (non-blocking mode), calling idle meanwhile. Returns its result. */
int fxio_async_wait(void (*idle)(void *ctx), void *ctx);

#endif
