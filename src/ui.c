#include <debug.h>
#include <delaythread.h>
#include <kernel.h>
#include <libpad.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

/* Pad handling adapted from ps2-usbhdl src/ui.c (b681bc6). */

static char pad_buf[256] __attribute__((aligned(64)));
static int pad_lib_inited;
static int pad_open;
static int prev_pressed;

void ui_delay_ms(int ms) { DelayThread(ms * 1000); }

void ui_init(void) {
  init_scr();
  scr_setCursor(0);
}

void ui_pad_close(void) {
  if (pad_open)
    padPortClose(0, 0);
  if (pad_lib_inited)
    padEnd();
  pad_open = pad_lib_inited = 0;
}

int ui_pad_open(void) {
  ui_pad_close();
  padInit(0);
  pad_lib_inited = 1;
  if (padPortOpen(0, 0, pad_buf) == 0)
    return 0;
  pad_open = 1;
  for (int tries = 0; tries < 40; tries++) {
    int s = padGetState(0, 0);
    if (s == PAD_STATE_STABLE || s == PAD_STATE_FINDCTP1) {
      prev_pressed = 0xFFFF; /* swallow buttons held at startup */
      return 1;
    }
    ui_delay_ms(100);
  }
  return 0;
}

static int pad_pressed(void) {
  struct padButtonStatus b;
  if (!pad_open)
    return 0;
  int s = padGetState(0, 0);
  if (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1)
    return 0;
  if (padRead(0, 0, &b) == 0)
    return 0;
  return (~b.btns) & 0xFFFF;
}

int ui_held_buttons(void) { return pad_pressed(); }

int ui_poll_button(void) {
  int p = pad_pressed();
  int newly = p & ~prev_pressed;
  prev_pressed = p;
  return newly;
}

int ui_wait_button(void) {
  if (!pad_open) {
    ui_delay_ms(3000);
    return UI_CROSS;
  }
  for (;;) {
    int n = ui_poll_button();
    if (n)
      return n;
    ui_delay_ms(30);
  }
}

void ui_clear(void) {
  scr_clear();
  scr_setXY(0, 0);
}

void ui_printf(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  scr_printf("%s", buf);
}

void ui_at(int row, const char *fmt, ...) {
  char buf[UI_ROW_LEN + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  scr_clearline(row);
  scr_setXY(0, row);
  scr_printf("%s", buf);
}

void ui_header(const char *title, const char *status) {
  ui_clear();
  ui_at(0, " UDPFS XMB Game Installer :: %s", title);
  ui_at(1, " ------------------------------------------------------------------------------");
  if (status && status[0])
    ui_at(2, " %s", status);
  scr_setXY(0, 3);
}

void ui_footer(const char *keys) { ui_at(UI_ROWS - 1, " %s", keys); }

int ui_select(const char *title, const char *status, char rows[][UI_ROW_LEN],
              int n, int start, const char *footer, int *key_out) {
  int idx = (start >= 0 && start < n) ? start : 0, top = 0, dirty = 1;
  if (key_out)
    *key_out = 0;
  ui_header(title, status);
  for (;;) {
    if (idx < top)
      top = idx;
    if (idx >= top + UI_LIST_ROWS)
      top = idx - UI_LIST_ROWS + 1;
    if (dirty) {
      for (int r = 0; r < UI_LIST_ROWS; r++) {
        int i = top + r;
        if (i < n)
          ui_at(4 + r, " %c %.*s", i == idx ? '>' : ' ', UI_COLS - 4, rows[i]);
        else
          ui_at(4 + r, "%s", "");
      }
      ui_at(4 + UI_LIST_ROWS, "   %s%s", top > 0 ? "[more above] " : "",
            top + UI_LIST_ROWS < n ? "[more below]" : "");
      if (n == 0)
        ui_at(4, "   (empty)");
      ui_footer(footer ? footer : "[Up/Down] move  [L1/R1] page  [X] select  [O] back");
      dirty = 0;
    }
    int b = ui_wait_button();
    if (n > 0 && (b & UI_UP)) {
      idx = (idx + n - 1) % n;
      dirty = 1;
    } else if (n > 0 && (b & UI_DOWN)) {
      idx = (idx + 1) % n;
      dirty = 1;
    } else if (n > 0 && (b & UI_L1)) {
      idx = idx >= UI_LIST_ROWS ? idx - UI_LIST_ROWS : 0;
      dirty = 1;
    } else if (n > 0 && (b & UI_R1)) {
      idx = idx + UI_LIST_ROWS < n ? idx + UI_LIST_ROWS : n - 1;
      dirty = 1;
    } else if (n > 0 && (b & UI_CROSS)) {
      return idx;
    } else if (key_out && n > 0 && (b & (UI_SQUARE | UI_START))) {
      *key_out = b & (UI_SQUARE | UI_START);
      return idx;
    } else if (b & (UI_CIRCLE | UI_TRIANGLE)) {
      return -1;
    }
  }
}

static void print_block(int row, const char *text) {
  scr_setXY(0, row);
  const char *p = text;
  while (*p && row < UI_ROWS - 2) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    ui_at(row++, " %.*s", len > UI_COLS - 2 ? UI_COLS - 2 : len, p);
    p = nl ? nl + 1 : p + len;
  }
}

void ui_message(const char *title, const char *text) {
  ui_header(title, NULL);
  print_block(3, text);
  ui_footer("[any button] continue");
  ui_wait_button();
}

int ui_confirm(const char *title, const char *text) {
  ui_header(title, NULL);
  print_block(3, text);
  ui_footer("[X] yes   [O] no");
  for (;;) {
    int b = ui_wait_button();
    if (b & UI_CROSS)
      return 1;
    if (b & (UI_CIRCLE | UI_TRIANGLE))
      return 0;
  }
}

int ui_confirm_destructive(const char *title, const char *text) {
  ui_header(title, NULL);
  print_block(3, text);
  ui_footer("HOLD [R1] and press [X] to confirm.   [O] cancel");
  if (!pad_open)
    return 0; /* never confirm destructive actions without a pad */
  for (;;) {
    int b = ui_wait_button();
    if ((b & UI_CROSS) && (ui_held_buttons() & UI_R1))
      return 1;
    if (b & (UI_CIRCLE | UI_TRIANGLE))
      return 0;
  }
}

static const char CHARSET[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    "-_.:'!&()+,/";

static int charset_index(char c) {
  const char *p = strchr(CHARSET, c);
  return (p && c) ? (int)(p - CHARSET) : 0;
}

int ui_edit_text(const char *title, const char *label, char *buf, size_t max) {
  char work[128];
  size_t cap = max < sizeof(work) ? max : sizeof(work);
  strncpy(work, buf, cap - 1);
  work[cap - 1] = 0;
  int cur = (int)strlen(work);
  int nchars = (int)strlen(CHARSET);

  ui_header(title, NULL);
  ui_at(4, " %s", label);
  ui_footer("[L/R] cursor [U/D] char [Sq] del [R1] space [X] save [O] cancel");
  for (;;) {
    int len = (int)strlen(work);
    if (cur > len)
      cur = len;
    ui_at(6, " [%s]", work);
    char caret[UI_ROW_LEN];
    memset(caret, ' ', sizeof(caret));
    caret[2 + cur] = '^';
    caret[3 + cur] = 0;
    ui_at(7, "%s", caret);
    ui_at(9, " %d/%d characters", len, (int)cap - 1);

    int b = ui_wait_button();
    if (b & UI_LEFT) {
      if (cur > 0)
        cur--;
    } else if (b & UI_RIGHT) {
      if (cur < len)
        cur++;
    } else if (b & (UI_UP | UI_DOWN)) {
      if (cur == len) { /* extend at the end */
        if (len >= (int)cap - 1)
          continue;
        work[len] = 'A';
        work[len + 1] = 0;
      } else {
        int i = charset_index(work[cur]);
        i = (b & UI_UP) ? (i + 1) % nchars : (i + nchars - 1) % nchars;
        work[cur] = CHARSET[i];
      }
    } else if (b & UI_SQUARE) {
      if (cur < len)
        memmove(work + cur, work + cur + 1, (size_t)(len - cur));
      else if (cur > 0)
        work[--cur] = 0;
    } else if (b & UI_R1) {
      if (len < (int)cap - 1) {
        memmove(work + cur + 1, work + cur, (size_t)(len - cur + 1));
        work[cur++] = ' ';
      }
    } else if (b & UI_CROSS) {
      strcpy(buf, work);
      return 1;
    } else if (b & (UI_CIRCLE | UI_TRIANGLE)) {
      return 0;
    }
  }
}
