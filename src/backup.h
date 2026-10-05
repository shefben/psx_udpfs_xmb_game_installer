#ifndef PSXI_BACKUP_H
#define PSXI_BACKUP_H

#include "xmb_game_channel.h"

/* Back up an installed game to a USB drive (browser.h USB_ROOT):
 *   PS2: the hidden HDL partition's data -> <USB>/DVD|CD/<ID>.<title>.iso
 *   PS1: IMAGE0.VCD of the channel      -> <USB>/POPS/<ID>.<title>.VCD
 * The CRC-32 of what was read from the HDD is compared with the install
 * journal's source CRC when there is one, then the USB file is read back
 * and compared (START skips that read-back). A partial file is removed.
 * rep->detail is the destination path on success. */
void backup_ps2_game(const char *hidden, const install_ui_t *ui, install_report_t *rep);
void backup_ps1_game(const char *partition, const install_ui_t *ui, install_report_t *rep);

#endif
