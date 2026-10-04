#ifndef PSXI_UI_H
#define PSXI_UI_H

#include <stddef.h>

/* Plain-text UI on the PS2SDK debug screen (80x27 characters). */

#define UI_ROWS 27
#define UI_COLS 79
#define UI_LIST_ROWS 17 /* visible rows in scrolling lists */
#define UI_ROW_LEN 80

/* Pad button bits (active high), re-exported from libpad. */
#define UI_UP 0x0010
#define UI_RIGHT 0x0020
#define UI_DOWN 0x0040
#define UI_LEFT 0x0080
#define UI_L2 0x0100
#define UI_R2 0x0200
#define UI_L1 0x0400
#define UI_R1 0x0800
#define UI_TRIANGLE 0x1000
#define UI_CIRCLE 0x2000
#define UI_CROSS 0x4000
#define UI_SQUARE 0x8000
#define UI_SELECT 0x0001
#define UI_START 0x0008

void ui_init(void);
/* (Re)open the pad after an IOP boot; returns 1 if a pad is usable. */
int ui_pad_open(void);
/* Release libpad before an IOP reset. */
void ui_pad_close(void);

void ui_clear(void);
/* Clear and draw a title bar plus optional status line. */
void ui_header(const char *title, const char *status);
void ui_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_at(int row, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void ui_footer(const char *keys);
void ui_delay_ms(int ms);

/* Block until a new button press; returns the newly pressed mask.
 * Without a pad it returns UI_CIRCLE (back/decline) after a short
 * delay: informational screens advance, nothing is ever accepted. */
int ui_wait_button(void);
/* Non-blocking poll for newly pressed buttons. */
int ui_poll_button(void);
/* Buttons currently held (no edge detection). */
int ui_held_buttons(void);

/* Scrolling single-select list. `rows` are preformatted lines.
 * Returns the selected index, or -1 if backed out (O / Triangle).
 * If `key_out` is non-NULL, Square and Start also return (with the
 * button in *key_out) so callers can attach extra actions. */
int ui_select(const char *title, const char *status, char rows[][UI_ROW_LEN],
              int n, int start, const char *footer, int *key_out);

/* Show lines and wait for any button. */
void ui_message(const char *title, const char *text);

/* Destructive confirmation: shows `text`, requires holding R1 while
 * pressing X. Returns 1 if confirmed, 0 otherwise. */
int ui_confirm_destructive(const char *title, const char *text);

/* Simple yes/no: X = yes, O = no. */
int ui_confirm(const char *title, const char *text);

/* D-pad text editor over a fixed charset. Left/Right move, Up/Down
 * change the character, Square deletes, R1 inserts a space, X saves,
 * O cancels. Returns 1 if saved. */
int ui_edit_text(const char *title, const char *label, char *buf, size_t max);

#endif
