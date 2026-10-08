#ifndef PSXI_EXTRAS_INSTALL_H
#define PSXI_EXTRAS_INSTALL_H

#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/* Puts a game's extras where the console software reads them (extras.h):
 *   PS2 (OPL):   CFG/<ID>.cfg, CHT/<ID>.cht (+ cheat keys), VMC/<ID>_<n>.bin
 *                (+ $VMC_n), ART/<ID>_<SUFFIX>.png - in the OPL partition
 *   PS1 (POPStarter): __common/POPS/<partition without PP.>/SLOT0.VMC,
 *                SLOT1.VMC and CHEATS.TXT
 * Sources: the server's CFG, CHT, VMC and ART folders (udpfsd), else the
 * same folders at the root of the USB drive. Existing memory cards are
 * never overwritten unless asked; settings only with replace_cfg. */

typedef struct {
  int replace_cfg;   /* the server's OPL cfg replaces the one on the HDD */
  int replace_cards; /* memory cards / saves replace existing ones */
} extras_opts_t;

typedef struct {
  int installed; /* files written */
  int kept;      /* left as they were (already there) */
  int failed;
  int found;     /* extras found for the game at the source */
  char what[160]; /* "settings, cheats, 2 cards, 5 art" */
  char note[160]; /* the first problem, "" if none */
} extras_report_t;

/* "udpfs:" when the server is up, else "mass0:" if a USB drive is in.
 * 0, or -1 if there is no source. */
int extras_source_root(char *out, size_t outsz);

void extras_install_ps2(const char *boot_id, const extras_opts_t *o, extras_report_t *r);
void extras_install_ps1(const char *partition, const char *boot_id, const extras_opts_t *o,
                        extras_report_t *r);

void extras_install_ps2_from(const char *root, const char *boot_id, const extras_opts_t *o, extras_report_t *r);
void extras_install_ps1_from(const char *root, const char *partition, const char *boot_id, const extras_opts_t *o, extras_report_t *r);

/* Verified temporary-file replacement, retaining the old file on failure. */
int extras_write_verified(const char *dst, const void *data, uint32_t len);

/* Single files (Saves menu). src is a full path ("udpfs:/VMC/x.bin").
 * *why is set on failure. */
inst_err_t extras_ps2_card_to_game(const char *src, const char *boot_id, int slot, int replace,
                                   const char **why);
inst_err_t extras_ps1_card_to_game(const char *src, const char *partition, int slot,
                                   int replace, const char **why);
inst_err_t extras_ps1_save_to_game(const char *src, const char *partition, int slot,
                                   int replace, const char **why);
/* A .psu save onto the memory card in slot `port` (0 or 1). */
inst_err_t extras_psu_to_card(const char *src, int port, int replace, const char **why);

#endif
