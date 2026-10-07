#ifndef PSXI_FLOWS_H
#define PSXI_FLOWS_H

#include "game_pair.h"
#include "xmb_game_channel.h"

/* Screens shared by the browser and the manage/repair menus. */

/* Install screen for one probed game: shows the plan, lets the user
 * edit the title, resolves existing-pair states, runs the install. */
void flow_install_game(game_plan_t *p);

/* Action menu for an existing pair; returns after one action. */
void flow_pair_actions(const char *visible, const char *hidden);

/* Error screen (plan section 32): stage, error, code, partition
 * presence and the safe recovery action. */
void flow_show_error(const char *what, const install_report_t *rep,
                     const char *recovery);

/* Install every selected game found in udpfs:/INSTALL (udpfsd
 * -install-dir), one after another, each through game_install(). */
void flow_batch_install(void);

/* Fully automatic install of every new game the server lists (udpfsd.cfg
 * auto_install = yes): countdown (O cancels), first-run installer
 * partition, every new game that fits, summary, then the system menu.
 * Returns only when cancelled or stopped before installing. */
void flow_auto_install(void);

void flow_installed_games(void);
/* Read every checkable installed game back (Verify game data for all). */
void flow_check_all_games(void);

/* flows_tools.c: install a PS2 game from the disc drive; HDD health
 * (SMART, space, largest game that fits, check all games). */
void flow_disc_install(void);
void flow_hdd_health(void);

/* flows_extras.c: saves / memory cards, cheats, OPL settings and art,
 * for installed games (from the server's VMC, CHT, CFG and ART folders). */
void flow_extras(void);
/* PS1 game (.VCD) from the server or USB: plan screen, then install. */
void flow_install_ps1(const char *path);
/* Rename / delete an installed PS1 game partition. */
void flow_ps1_actions(const char *partition);
/* Once at start-up: hooks the UI into the HDD layer (128 GiB warning). */
void flows_init(void);

/* Install a homebrew .ELF (udpfs: or USB) as an XMB app channel. */
void flow_install_app(const char *elf_path);

/* Apps submenu: install from UDPFS / USB, installed apps (delete). */
void flow_apps(void);

/* Select several installed games and delete both partitions of each. */
void flow_remove_games(void);
void flow_repair(void);
void flow_network_settings(void);
void flow_self_install(void);

/* Remove the installer partition (channel, journals, settings) after an
 * explicit warning. */
void flow_delete_installer_channel(void);


#endif
