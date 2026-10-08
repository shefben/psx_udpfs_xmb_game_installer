#ifndef PSXI_POPS_H
#define PSXI_POPS_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"
#include "partname.h"
#include "source.h"

/* PS1 games for POPStarter (krHACKen). A PS1 game is one PFS partition
 * PP.<ID>..<TITLE> holding EXECUTE.KELF (POPSTARTER.KELF), IMAGE0.VCD
 * and the XMB res/ files; POPStarter opens IMAGE0.VCD in the partition
 * it was started from. POPS.ELF and IOPRP252.IMG (Sony, supplied by the
 * user) live in __common/POPS/, VMCs in __common/POPS/<name without PP.>/.
 * Pure parts; the install flow is pops_install.c. */

#define VCD_HEADER_SIZE 0x100000u /* cue2pops header */
#define VCD_RAW_SECTOR 2352u
#define POPS_IMAGE "IMAGE0.VCD"
#define POPS_DISCS "DISCS.TXT"
#define POPS_KELF "POPSTARTER.KELF"
#define POPS_ELF "POPS.ELF"
#define POPS_IOPRP "IOPRP252.IMG"

typedef struct {
  char boot_id[16];  /* "SLUS_005.94" */
  char part_id[PART_ID_LEN + 1];
  char volume_id[33];
  uint32_t sectors;  /* raw 2352-byte sectors after the header */
  uint64_t bytes;    /* whole VCD file */
} vcd_info_t;

/* Check the VCD layout and read the game ID from SYSTEM.CNF (BOOT line)
 * through ISO9660 on Mode 2 Form 1 sectors. ERR_OK,
 * ERR_SOURCE_INVALID_ISO (not a VCD / no ISO9660), ERR_SOURCE_SYSTEM_CNF
 * (no SYSTEM.CNF or no valid game ID), ERR_SOURCE_READ. */
inst_err_t vcd_probe(GameSource *src, vcd_info_t *out);

/* Partition size for VCDs of `vcd_bytes` in all (+ 8 MiB for the KELF
 * and res/): 128M, 256M, 512M, 1G, 2G or 4G (multi-disc games). Returns
 * MiB (and the APA size string), or -1 if too big. */
int pops_partition_mb(uint64_t vcd_bytes, char *size_str, size_t sz);

/* Multi-disc games: every disc in the one partition as IMAGE0..3.VCD,
 * listed in DISCS.TXT (one name per line), which POPStarter reads to
 * change discs (Select+L2+R2 with Triangle, Up/Right/Down/Left, Square). */
#define POPS_MAX_DISCS 4
const char *pops_image_name(int disc); /* 0 -> "IMAGE0.VCD" */
/* DISCS.TXT for n (2..4) discs. Length, or -1. */
int pops_discs_txt(int n, char *out, size_t outsz);
/* Disc number (1..4) from a file name ("Game (Disc 2).VCD", "Game CD2.VCD"),
 * 0 if it has none. stem: the name without that word and the extension,
 * equal for every disc of one game. */
int pops_disc_number(const char *name, char *stem, size_t stemsz);

/* VMC folder under __common/POPS/: the partition name without "PP.". */
void pops_vmc_dir(const char *partition, char *out, size_t outsz);

#endif
