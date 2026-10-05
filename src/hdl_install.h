#ifndef PSXI_HDL_INSTALL_H
#define PSXI_HDL_INSTALL_H

#include <stdint.h>

#include "errors.h"
#include "hdl_header.h"
#include "hdl_plan.h"
#include "iso9660.h"
#include "source.h"

/* Hidden HDL game-data partition: create, format, stream, verify
 * (plan sections 12, 13, 33). Base: ps2-usbhdl src/hdl.c (b681bc6). */

#define STREAM_BUF_SIZE (1024 * 1024)

typedef struct {
  /* Called at most about once per second. */
  void (*progress)(void *ctx, uint64_t done, uint64_t total, uint32_t elapsed_s);
  /* Return non-zero to abort (polled with progress). */
  int (*should_abort)(void *ctx);
  void *ctx;
  /* hdl_stream only, optional: called about every STREAM_CHECKPOINT
   * bytes, after those bytes were written, with the CRC-32 of all bytes
   * so far. Non-zero return stops the copy (journal not saved). */
  int (*checkpoint)(void *cp_ctx, uint64_t bytes, uint32_t crc);
  void *cp_ctx;
} stream_cb_t;

#define STREAM_CHECKPOINT (256ull * 1024 * 1024)

/* Where the time of the running hdl_stream()/hdl_verify() goes, in EE
 * bus clock ticks (GetTimerSystemTime, STREAM_TIMER_HZ per second).
 * Reset when each starts; read by the progress screen. */
#define STREAM_TIMER_HZ 147456000u
typedef struct {
  uint64_t bytes;
  uint64_t read_ticks;  /* UDPFS read (stream) or HDD read (verify) */
  uint64_t crc_ticks;   /* CRC-32 on the EE */
  uint64_t write_ticks; /* HDD write (stream only) */
} stream_timing_t;
extern stream_timing_t g_stream_timing;

typedef struct {
  inst_err_t err;
  int rc;         /* underlying driver code */
  uint64_t bytes; /* bytes streamed / read back */
  uint32_t crc32; /* CRC-32 of those bytes */
} hdl_result_t;

/* Create `hidden` sized by `alloc` and write the HDL header via
 * hdlfs format. Removes the half-made partition on failure. */
hdl_result_t hdl_create_and_format(const char *hidden, const hdl_alloc_t *alloc,
                                   const struct HDLFS_FormatArgs *args);

/* Stream bytes [start, total) from `src` into the formatted `hidden`
 * partition, computing the CRC-32 of every byte (continuing from
 * `start_crc`, the CRC of the first `start` bytes; 0/0 for a new copy).
 * `start` must be a multiple of 2048. Always closes and unmounts hdl0:.
 * result.bytes counts from 0 (includes `start`). */
/* Use hddpump.irx for hdl_stream when set (module loaded and enabled in
 * Network Settings); otherwise, or if the pump cannot start, the EE loop. */
extern int g_hdl_use_pump;

hdl_result_t hdl_stream(const char *hidden, GameSource *src, uint64_t total, uint64_t start,
                        uint32_t start_crc, const stream_cb_t *cb);

/* Verify without the source: APA type HDL, HDL header fields, then a
 * read-only remount and a sequential read of all installed data
 * (CRC-32 + PVD/volume-id check at sector 16). The caller requires
 * bytes == expected and crc32 == the stream CRC. */
hdl_result_t hdl_verify(const char *hidden, const iso_info_t *iso,
                        int expected_parts, const stream_cb_t *cb);

/* Read back `total` installed bytes (APA type HDL checked) and return
 * their CRC-32: "Verify game data" for an install whose journal holds
 * the source CRC. */
hdl_result_t hdl_read_back(const char *hidden, uint64_t total, const stream_cb_t *cb);

/* Header-only read for repair/manage scans. 0 or <0. */
int hdl_read_header(const char *hidden, hdl_header_info_t *out);

/* Physical identity of a hidden partition for the journal: APA start
 * sector, size in sectors, CRC-32 of the raw 1 KiB HDL header. 0 or <0. */
int hdl_partition_identity(const char *hidden, uint32_t *start, uint32_t *size,
                           uint32_t *header_crc32);

/* Header sanity for scans: HDL type, header parses, valid startup id,
 * non-empty title. Says nothing about whether the data is complete. */
int hdl_partition_looks_valid(const char *hidden, hdl_header_info_t *out);

#endif
