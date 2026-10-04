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

void flow_installed_games(void);
void flow_repair(void);
void flow_network_settings(void);
void flow_self_install(void);


#endif
