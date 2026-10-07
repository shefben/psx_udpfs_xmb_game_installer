#ifndef PSXI_SOURCE_H
#define PSXI_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Logical-file abstraction (plan section 6). All offsets/sizes are
 * 64-bit; the ISO parser and stream loop only ever talk to this. */

#define SOURCE_PATH_MAX 256

#define SRC_SEEK_SET 0
#define SRC_SEEK_CUR 1
#define SRC_SEEK_END 2

typedef struct GameSource GameSource;

typedef struct {
  /* 0 on success, negative driver code on failure. */
  int (*open)(GameSource *src, const char *path);
  int (*close)(GameSource *src);
  /* >0 bytes read, 0 at EOF, negative driver code on error. May return
   * fewer bytes than asked; source_read_exact() loops. */
  int (*read)(GameSource *src, void *buf, uint32_t size);
  /* New absolute position, or negative on error. */
  int64_t (*seek)(GameSource *src, int64_t offset, int whence);
  /* Logical size in bytes, or negative on error. */
  int64_t (*size)(GameSource *src);
  /* Optional (NULL: unsupported). Background read-ahead for copy loops:
   * SRC_ASYNC_ON starts it, SRC_ASYNC_SYNC waits for the read in flight
   * (keeping its data) so the caller may use fileXio, SRC_ASYNC_OFF stops
   * it. While a read is in flight nothing else may use fileXio; `idle`
   * (may be NULL) is called repeatedly while one is awaited. */
  void (*set_async)(GameSource *src, int mode, void (*idle)(void *ctx), void *ctx);
  /* Optional: up to `max` bytes at the current position without a copy.
   * *out stays valid until the next call on this source. >0 bytes, 0 at
   * EOF, negative driver code on error. */
  int (*read_ptr)(GameSource *src, uint32_t max, const uint8_t **out);
} GameSourceOps;

#define SRC_ASYNC_OFF 0
#define SRC_ASYNC_ON 1
#define SRC_ASYNC_SYNC 2

struct GameSource {
  const GameSourceOps *ops;
  void *priv;
  int is_open;
  int last_rc; /* last negative driver code, for error screens */
  char path[SOURCE_PATH_MAX];
};

typedef enum {
  SRC_TYPE_NONE = 0, /* not shown in the browser */
  SRC_TYPE_ISO,
  SRC_TYPE_ZSO, /* udpfsd virtual "<name>.zso.iso" */
  SRC_TYPE_ZSO_FILE, /* a raw "<name>.zso" (USB), decompressed by source_zso */
  SRC_TYPE_VCD, /* PS1 game for POPStarter (pops.h) */
  SRC_TYPE_CSO, /* udpfsd virtual "<name>.cso.iso" */
  SRC_TYPE_CHD, /* udpfsd virtual "<name>.chd.iso" (CHD-enabled udpfsd) */
} source_type_t;

/* Classify a UDPFS directory entry by name only (plan section 30).
 * ".zso.iso" -> ZSO, ".cso.iso" -> CSO, ".chd.iso" -> CHD (udpfsd's
 * virtual images), any other ".iso" -> ISO. Case-insensitive. */
source_type_t source_classify(const char *name);
const char *source_type_label(source_type_t t); /* "ISO", "ZSO", "" */

/* Name shown in lists: udpfsd serves a .zso/.cso as "<name>.zso.iso";
 * show "<name>.zso". Other names unchanged. Returns out. */
const char *source_display_name(const char *name, char *out, size_t outsz);

/* A raw ZSO file (not udpfsd's virtual .zso.iso): needs source_zso. */
int source_is_raw_zso(const char *path);

/* Thin wrappers that keep error codes distinct. */
inst_err_t source_open(GameSource *src, const char *path);
void source_close(GameSource *src);
int64_t source_size(GameSource *src);

/* Read exactly `size` bytes. ERR_OK, or ERR_SOURCE_READ for a driver
 * error *or* a premature EOF (short read is an error, plan 6). */
inst_err_t source_read_exact(GameSource *src, void *buf, uint32_t size);

/* Seek to `offset` (SEEK_SET) then read exactly `size` bytes. */
inst_err_t source_read_at(GameSource *src, uint64_t offset, void *buf,
                          uint32_t size);

/* set_async if the source has it (no-op otherwise). */
void source_async(GameSource *src, int mode, void (*idle)(void *ctx), void *ctx);

/* Next block for a copy loop: `want` bytes (a multiple of 2048, or the
 * rest of the source) or fewer, but always a multiple of 2048 unless it
 * is exactly `want`. Lent from the source's buffer when it can (read_ptr),
 * else read into `buf` (>= want bytes). ERR_OK with *out and *got, or
 * ERR_SOURCE_READ (driver error or premature EOF). */
inst_err_t source_next_block(GameSource *src, uint8_t *buf, uint32_t want,
                             const uint8_t **out, uint32_t *got);

/* ---- In-memory sparse source, used by host tests ----------------
 * A virtual file of `total_size` bytes that is all zeros except for
 * the given segments. Lets tests model multi-GiB images cheaply. */
#define MEMSRC_MAX_SEGS 16
typedef struct {
  uint64_t offset;
  const uint8_t *data;
  uint32_t len;
} memsrc_seg_t;

typedef struct {
  uint64_t total_size;
  memsrc_seg_t segs[MEMSRC_MAX_SEGS];
  int nsegs;
  uint64_t pos;
  int fail_after_reads; /* >0: return an error on that read call */
  int reads;
  uint32_t max_chunk; /* >0: cap bytes per read call */
} memsrc_t;

void memsrc_init(GameSource *src, memsrc_t *m, uint64_t total_size);
void memsrc_add(memsrc_t *m, uint64_t offset, const void *data, uint32_t len);

#endif
