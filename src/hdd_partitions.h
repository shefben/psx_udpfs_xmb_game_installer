#ifndef PSXI_HDD_PARTITIONS_H
#define PSXI_HDD_PARTITIONS_H

#include <stdint.h>

#include "errors.h"
#include "partname.h"
#include "space.h"

/* APA partition helpers on the internal HDD (PS2 only). */

#define PFS_APP "pfs0:"  /* installer application partition, kept mounted */
#define PFS_WORK "pfs1:" /* short-lived work mounts */
#define INSTALLER_PARTITION INSTALLER_PARTITION_NAME
#define CHANNEL_SIZE_STR "128M"
#define CHANNEL_SIZE_MB 128

typedef struct {
  char name[APA_NAME_MAX + 1];
  uint16_t type;
  uint32_t size_sectors; /* 512-byte sectors, as reported by dread */
} hdd_part_t;

/* ERR_OK, ERR_HDD_MISSING or ERR_HDD_NOT_FORMATTED. */
inst_err_t hdd_status(void);

/* Fresh (uncached) totals in MB, using libhdd's dread summation.
 * free_mb is the space usable for new games and data: the HDD's free
 * space, but never more than the 128 GiB limit leaves (space.h). */
int hdd_space_mb(uint32_t *total_mb, uint32_t *free_mb, uint32_t *max_part_mb);

/* The same without the limit: what the drive itself has free. */
int hdd_space_raw(uint32_t *total_mb, uint32_t *free_mb, uint32_t *max_part_mb);

#define SPACE_MAX_PARTS 512
/* The APA list as space_part_t (valid until the next call). Count or <0. */
int hdd_space_list(const space_part_t **out);

/* Games / games+data / system usage, and the drive's own free MiB. */
int hdd_usage(space_usage_t *u, uint32_t *hdd_free_mb);

/* Room for add_mb more: ERR_OK, ERR_NO_SPACE, ERR_DATA_LIMIT. */
inst_err_t hdd_space_check(uint32_t add_mb);

/* After creating `name`: if any of its segments ends beyond 128 GiB it is
 * removed again and ERR_DATA_LIMIT returned; else ERR_OK. */
inst_err_t hdd_space_guard_new(const char *name, int *rc_out);

/* Enumerate main partitions. Returns count (<= max) or <0. */
int hdd_list(hdd_part_t *out, int max);

/* 1 exists, 0 absent, <0 enumeration failure. Exact name match. */
int hdd_exists(const char *name);

/* Stat a partition: 0 and fills type/size/start, <0 driver code. */
int hdd_stat(const char *name, uint16_t *type, uint32_t *size_sectors,
             uint32_t *start_sector);

/* Rename the installer's legacy partition (INSTALLER_LEGACY_NAME) to
 * INSTALLER_PARTITION when only the legacy one exists (it must not be
 * mounted). 0 if nothing was needed or the rename was confirmed, <0
 * driver code otherwise (the legacy partition is then still intact). */
int installer_partition_migrate(void);

/* Remove exactly `name` (never by prefix) and confirm it is gone.
 * ERR_OK if gone (or was already absent), ERR_PARTITION_DELETE. */
inst_err_t hdd_remove_exact(const char *name, int *rc_out);

/* Show a game in the XMB ("__.X" -> "PP.X") or hide it ("PP.X" ->
 * "__.X"): an APA rename, the data stays where it is. Both names must
 * be the two names of one game, the source an HDL partition and the
 * target absent. Needs the patched driver (patches/apa-hdl/0002).
 * ERR_OK (verified by listing), ERR_INVALID_ARG, ERR_PARTITION_EXISTS
 * or ERR_PARTITION_RENAME. */
inst_err_t hdd_rename_game(const char *from, const char *to, int *rc_out);

/* Create a 128 MiB PFS partition and format it. ERR_OK,
 * ERR_PARTITION_EXISTS, ERR_PFS_CREATE or ERR_PFS_FORMAT. A
 * half-created partition is removed on format failure. */
inst_err_t pfs_create_partition(const char *name, const char *size_str,
                                int *rc_out);

/* Mount/unmount; mount returns 0 or driver code. Unmount is
 * idempotent. */
int pfs_mount(const char *mountpoint, const char *partition, int mode);
void pfs_umount(const char *mountpoint);

/* Write a whole buffer to a file (create/truncate). 0 or <0. */
int file_write_all(const char *path, const void *data, uint32_t len);

/* Load a whole file into a malloc'd buffer (caller frees). Returns
 * size or <0. `max` bounds the allocation. */
int file_load(const char *path, void **out, uint32_t max);

/* Size of a file via getstat; <0 if absent. */
int64_t file_size(const char *path);

/* Size by opening the file (open + seek to end). For udpfs: paths:
 * udpfsd answers getstat only for files that are already open, so
 * file_size() reports every other file there as missing. */
int64_t file_open_size(const char *path);

#endif
