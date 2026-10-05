#ifndef PSXI_PUMP_H
#define PSXI_PUMP_H

#include <stdint.h>

/* EE client of hddpump.irx (iop/hddpump): the IOP writes block n to the
 * HDD on its own thread while the EE fetches block n+1 from the network.
 * Every call returns 0 or a negative IOP/driver code. */

/* Bind the RPC (once per IOP boot). 0 when the module answers. */
int pump_init(void);

/* Open `path` (already mounted, e.g. "hdl0:") for writing at 2048-byte
 * sector `start_sector`, with up to `nslots` IOP buffers of `slot_size`.
 * *slot_size_out: the size actually used (a block must not exceed it). */
int pump_begin(const char *path, uint32_t start_sector, uint32_t slot_size, int nslots,
               uint32_t *slot_size_out);

/* Copy `len` bytes (multiple of 16) to the current IOP buffer and queue
 * it; blocks only while every buffer waits for the HDD. */
int pump_put(const void *buf, uint32_t len);

/* Wait until everything queued is on the HDD. */
int pump_flush(void);

/* Flush, close and free; *written = bytes written since pump_begin. */
int pump_end(uint64_t *written);

#endif
