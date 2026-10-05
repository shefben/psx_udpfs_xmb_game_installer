#ifndef PSXI_UI_STYLE_H
#define PSXI_UI_STYLE_H

#include <stdint.h>

/* Screen geometry and text styling (pure; drawing is in ui.c).
 *
 * The PS2SDK debug screen is 80x28 cells of 8x8 pixels (640x224, every
 * line shown twice). The UI uses a smaller grid inside it so nothing
 * falls into TV overscan: logical row r / column c is drawn at cell
 * (UI_MARGIN_X + c, UI_MARGIN_Y + r). */

#define UI_SCREEN_COLS 80
#define UI_SCREEN_ROWS 28
#define UI_MARGIN_X 3
#define UI_MARGIN_Y 1
#define UI_ROWS 26     /* logical rows: 0..2 header, UI_ROWS-2..-1 footer */
#define UI_COLS 74     /* logical columns */
#define UI_LIST_ROWS 16 /* visible rows in scrolling lists */
#define UI_ROW_LEN 80  /* row buffers (text beyond UI_COLS is cut) */

/* Glyph rows are drawn bold: every 1-pixel vertical stroke becomes two
 * pixels wide, which survives composite/component TV output. */
uint8_t ui_bold_row(uint8_t bits);

typedef enum {
  UI_TONE_NORMAL = 0,
  UI_TONE_GOOD, /* PASS, OK, installed */
  UI_TONE_WARN, /* WARN, WARNING */
  UI_TONE_BAD,  /* FAIL, FAILED, ERROR */
} ui_tone_t;

/* Colour of a body line from its first word (after leading spaces). */
ui_tone_t ui_line_tone(const char *line);

#endif
