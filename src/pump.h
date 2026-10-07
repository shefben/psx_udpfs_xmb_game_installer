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

/* ATA device 0 as atad sees it, plus IDENTIFY words 121-124. */
typedef struct {
  int exists, lba48, identify;
  uint32_t sectors;
  uint16_t sig[4];
} pump_atainfo_t;

/* 0, or <0 when the pump module is not running. */
int pump_atainfo(pump_atainfo_t *out);

/* Read-back while a copy runs (between pump_begin and pump_end): the
 * IOP reads `len` bytes (multiple of 2048, <= slot size) at 2048-byte
 * sector `sector` of the open partition, after the writes queued so far,
 * into `dst` (64-byte aligned; do not touch it until done). One at a
 * time. pump_read_start: 0 started, -16 one is running, <0 error.
 * pump_read_poll: 1 done (*got = bytes or <0), 0 still running.
 * pump_read_wait: waits for it, then like poll. */
int pump_read_start(uint32_t sector, void *dst, uint32_t len);
int pump_read_poll(int *got);
int pump_read_wait(int *got);

/* SMART of ATA device 0: status 0 ok, 1 threshold exceeded, <0 failed;
 * data_rc 0 when data[] (SMART READ DATA) is valid. 0, or <0 when the
 * pump module is not running. */
typedef struct {
  int status, data_rc;
  uint8_t data[512];
} pump_smart_t;
int pump_smart(pump_smart_t *out);

/* Free IOP memory: total and largest block. 0 or <0. */
int pump_meminfo(uint32_t *free_bytes, uint32_t *max_block);

#endif
