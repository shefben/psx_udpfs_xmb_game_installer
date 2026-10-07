#ifndef PSXI_SOURCE_CDVD_H
#define PSXI_SOURCE_CDVD_H

#include "source.h"

/* GameSource over the console's own disc drive (libcdvd sceCdRead,
 * 2048-byte user data): a PS2 CD or DVD, DVD-9 included. Its logical
 * size is the disc's ISO size (layer 0 PVD, plus layer 1's PVD on a
 * dual-layer disc), so the copy is byte-identical to a dump of the disc.
 * With set_async on, the next CDVD_RA_SECTORS are read while the caller
 * works on the current block. Path: CDVD_PATH. */

#define CDVD_PATH "cdrom0:"
#define CDVD_RA_SECTORS 128 /* 256 KiB per read */
/* open: a DVD-9 whose layers do not fit together (never copied as one). */
#define CDVD_ERR_LAYERS (-2000)

/* Disc in the drive, by libcdvd's disk type. */
typedef enum {
  CDVD_DISC_NONE = 0, /* empty, or still spinning up after the timeout */
  CDVD_DISC_PS2_CD,
  CDVD_DISC_PS2_DVD,
  CDVD_DISC_PS1,      /* a PlayStation disc: not installable here */
  CDVD_DISC_OTHER,    /* video DVD, audio CD, unknown */
  CDVD_DISC_NO_DRIVE, /* cdvdfsv not answering */
} cdvd_disc_t;

/* Wait (up to ~timeout_ms) for the drive to settle and classify the disc. */
cdvd_disc_t cdvd_disc_detect(int timeout_ms);

/* Classify a libcdvd disk type (pure, for host tests). */
cdvd_disc_t cdvd_classify(int disk_type);

typedef struct {
  int64_t pos, size;
  int dvd;
  int async;
  void (*idle)(void *ctx);
  void *idle_ctx;
} cdvd_src_t;

void source_cdvd_init(GameSource *src, cdvd_src_t *c);

/* Open the drive's tray (after an install, on request). */
void cdvd_eject(void);

/* The disc's layers as the drive reports them: *dual 1 for a DVD-9,
 * *layer1_start its first sector. 0, or <0 if the drive did not say. */
int cdvd_dual_info(int *dual, uint32_t *layer1_start);

#endif
