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
  /* Called at most a few times per second while streaming. */
  void (*progress)(void *ctx, uint64_t done, uint64_t total, uint32_t elapsed_s);
  /* Return non-zero to abort (polled with progress). */
  int (*should_abort)(void *ctx);
  void *ctx;
} stream_cb_t;

typedef struct {
  inst_err_t err;
  int rc;           /* underlying driver code */
  uint64_t written; /* bytes written before the error */
} hdl_result_t;

/* Create `hidden` sized by `alloc` and write the HDL header via
 * hdlfs format. Removes the half-made partition on failure. */
hdl_result_t hdl_create_and_format(const char *hidden, const hdl_alloc_t *alloc,
                                   const struct HDLFS_FormatArgs *args);

/* Stream exactly `total` bytes from `src` (positioned anywhere) into
 * the formatted `hidden` partition. Always closes and unmounts hdl0:. */
hdl_result_t hdl_stream(const char *hidden, GameSource *src, uint64_t total,
                        const stream_cb_t *cb);

/* Read back and verify: APA type HDL, header fields, exact data size,
 * and that the installed PVD and last sector match the source. */
hdl_result_t hdl_verify(const char *hidden, GameSource *src,
                        const iso_info_t *iso, int expected_parts);

/* Header-only read for repair/manage scans. 0 or <0. */
int hdl_read_header(const char *hidden, hdl_header_info_t *out);

/* Loose validity for games found on disk (may be from other tools):
 * HDL type, header parses, valid startup id, non-empty title. */
int hdl_partition_looks_valid(const char *hidden, hdl_header_info_t *out);

#endif
