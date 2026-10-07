#ifndef PSXI_POPS_INSTALL_H
#define PSXI_POPS_INSTALL_H

#include "pops.h"
#include "xmb_game_channel.h"

/* Install a PS1 VCD as a POPStarter XMB channel (see pops.h). */

typedef struct {
  char source_path[SOURCE_PATH_MAX];
  vcd_info_t vcd;
  char title[64];
  char partition[APA_NAME_MAX + 1];
  char size_str[8];
  int size_mb;
  /* 1, or 2..POPS_MAX_DISCS: every disc as IMAGE<n>.VCD + DISCS.TXT */
  int ndiscs;
  char disc_path[POPS_MAX_DISCS][SOURCE_PATH_MAX];
  uint64_t disc_bytes[POPS_MAX_DISCS];
} pops_plan_t;

/* Probe the VCD and build the plan (name PP.<ID>..<TITLE>). */
inst_err_t pops_plan_build(const char *path, pops_plan_t *p, int *rc_out);
inst_err_t pops_plan_set_title(pops_plan_t *p, const char *title);

/* The other discs of a multi-disc game next to the chosen VCD ("Game
 * (Disc 2).VCD", disc 1..n all present): the plan becomes the whole set,
 * named and sized from disc 1. Returns the number of discs (1: unchanged). */
int pops_plan_find_discs(pops_plan_t *p);

/* Copy POPS.ELF / IOPRP252.IMG to __common/POPS when missing, create the
 * game partition, write EXECUTE.KELF (POPSTARTER.KELF), IMAGE0.VCD
 * (CRC-32 while copying), res/, read IMAGE0.VCD back and compare
 * (START may skip, as for PS2 games), and only then the XMB header. On
 * failure the partition is removed again. The POPS files are taken from
 * the VCD's folder or <device>/POPS/. */
void pops_install(pops_plan_t *p, const install_ui_t *ui, install_report_t *rep);

/* A PFS channel that holds IMAGE0.VCD (a PS1 game, no __. partner). */
int pops_partition_is_ps1(const char *partition);

#endif
