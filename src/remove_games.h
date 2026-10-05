#ifndef PSXI_REMOVE_GAMES_H
#define PSXI_REMOVE_GAMES_H

#include <stddef.h>

#include "errors.h"
#include "game_pair.h"
#include "partname.h"

/* Remove Games: pick several installed games (any pair state) and delete
 * both partitions of each. Pure selection/summary logic; the console flow
 * is in flows.c. */

typedef enum { REMOVE_PENDING = 0, REMOVE_DONE, REMOVE_FAILED } remove_result_t;

typedef struct {
  char visible[APA_NAME_MAX + 1];
  char hidden[APA_NAME_MAX + 1];
  pair_state_t state;
  int selected;
  remove_result_t result;
  inst_err_t err;
  const char *failed; /* partition (or "journal") that could not be removed */
  int rc;
} remove_entry_t;

void remove_entry_init(remove_entry_t *e, const char *visible, const char *hidden,
                       pair_state_t state);

/* Returns the new selected state. */
int remove_toggle(remove_entry_t *e);

/* All selected: select none; otherwise select all. Returns the count. */
int remove_toggle_all(remove_entry_t *e, int n);

int remove_count_selected(const remove_entry_t *e, int n);

/* One-line row for the selection list (<= 70 chars). */
void remove_format_row(const remove_entry_t *e, char *out, size_t outsz);

/* Summary text: counts, then one line per selected entry. */
size_t remove_summary(const remove_entry_t *e, int n, char *out, size_t outsz);

#endif
