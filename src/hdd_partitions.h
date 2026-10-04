#ifndef PSXI_HDD_PARTITIONS_H
#define PSXI_HDD_PARTITIONS_H

#include <stdint.h>

#include "errors.h"
#include "partname.h"

/* APA partition helpers on the internal HDD (PS2 only). */

#define PFS_APP "pfs0:"  /* installer application partition, kept mounted */
#define PFS_WORK "pfs1:" /* short-lived work mounts */
#define INSTALLER_PARTITION "PP.UDPFS-INSTALLER"
#define CHANNEL_SIZE_STR "128M"
#define CHANNEL_SIZE_MB 128

#define APA_TYPE_PFS_ 0x0100
#define APA_TYPE_HDL_ 0x1337

typedef struct {
  char name[APA_NAME_MAX + 1];
  uint16_t type;
  uint32_t size_sectors; /* 512-byte sectors, as reported by dread */
} hdd_part_t;

/* ERR_OK, ERR_HDD_MISSING or ERR_HDD_NOT_FORMATTED. */
inst_err_t hdd_status(void);

/* Fresh (uncached) totals in MB, using libhdd's dread summation. */
int hdd_space_mb(uint32_t *total_mb, uint32_t *free_mb, uint32_t *max_part_mb);

/* Enumerate main partitions. Returns count (<= max) or <0. */
int hdd_list(hdd_part_t *out, int max);

/* 1 exists, 0 absent, <0 enumeration failure. Exact name match. */
int hdd_exists(const char *name);

/* Stat a partition: 0 and fills type/size/start, <0 driver code. */
int hdd_stat(const char *name, uint16_t *type, uint32_t *size_sectors,
             uint32_t *start_sector);

/* Remove exactly `name` (never by prefix) and confirm it is gone.
 * ERR_OK if gone (or was already absent), ERR_PARTITION_DELETE. */
inst_err_t hdd_remove_exact(const char *name, int *rc_out);

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

#endif
