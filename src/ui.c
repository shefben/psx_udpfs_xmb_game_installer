#include <debug.h>
#include <delaythread.h>
#include <ee_regs.h>
#include <kernel.h>
#include <libpad.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

/* ------------------------------------------------------------------ */
/* Text drawing                                                        */
/*                                                                     */
/* init_scr() (PS2SDK libdebug) sets up the video mode and a 640x224   */
/* frame buffer, as PS2BBL does. Letters are libdebug's 8x8 MSX font,  */
/* drawn bold, a whole 640x8 text row per GS upload (same GIF packet   */
/* layout as libdebug's scr_putchar, ps2sdk ee/debug, AFL 2.0).        */
/* ------------------------------------------------------------------ */

extern const unsigned char msx[]; /* libdebug font, 8 bytes per char */

/* PSMCT32 colours, 0x00BBGGRR */
#define C_BG 0x00301808     /* dark navy */
#define C_TEXT 0x00E8E8E8   /* body text */
#define C_DIM 0x009C9C9C    /* status, hints */
#define C_TITLE 0x0040E8FF  /* yellow */
#define C_BAR 0x00883410    /* title bar blue */
#define C_RULE 0x00A05828   /* separator lines */
#define C_ACCENT 0x00F0D060 /* cyan: button hints */
#define C_SEL_BG 0x00B05818 /* selected row */
#define C_SEL_FG 0x0040F0FF
#define C_GOOD 0x0060E070
#define C_WARN 0x0030B8FF
#define C_BAD 0x005050FF

static u32 rowbuf[UI_SCREEN_COLS * 8 * 8] __attribute__((aligned(64)));
static u64 gif_setup[12] __attribute__((aligned(16)));

static void dma_gif(const void *addr, int qwc) {
  *R_EE_D2_QWC = (u32)qwc;
  *R_EE_D2_MADR = (u32)addr;
  *R_EE_D2_CHCR = 0x101; /* from memory, start */
  while (*R_EE_D2_CHCR & 0x100)
    ;
}

/* Upload rowbuf as screen text row prow (0..UI_SCREEN_ROWS-1). */
static void blit_row(int prow) {
  const int w = UI_SCREEN_COLS * 8, qwc = w * 8 * 4 / 16;
  gif_setup[0] = 0x1000000000000004ULL; /* GIFtag: A+D, NLOOP 4 */
  gif_setup[1] = 0xE;
  gif_setup[2] = (u64)(w / 64) << 48;   /* BITBLTBUF: DBP 0, DBW, PSMCT32 */
  gif_setup[3] = 0x50;
  gif_setup[4] = (u64)(prow * 8) << 48; /* TRXPOS: DSAX 0, DSAY */
  gif_setup[5] = 0x51;
  gif_setup[6] = (8ULL << 32) | (u64)w; /* TRXREG: RRW, RRH 8 */
  gif_setup[7] = 0x52;
  gif_setup[8] = 0; /* TRXDIR: host -> local */
  gif_setup[9] = 0x53;
  gif_setup[10] = 0x0800000000008000ULL | (u64)qwc; /* GIFtag: IMAGE, EOP */
  gif_setup[11] = 0;
  FlushCache(0);
  dma_gif(gif_setup, 6);
  dma_gif(rowbuf, qwc);
}

/* Draw logical row lrow: text from logical column 0 in fg on bg; the
 * TV margins stay in the screen background. */
static void put_row(int lrow, const char *text, u32 fg, u32 bg) {
  if (lrow < 0 || lrow >= UI_ROWS)
    return;
  size_t len = text ? strlen(text) : 0;
  for (int cell = 0; cell < UI_SCREEN_COLS; cell++) {
    int c = cell - UI_MARGIN_X;
    int inside = c >= 0 && c < UI_COLS;
    unsigned char ch = inside && (size_t)c < len ? (unsigned char)text[c] : ' ';
    if (ch < 32 || ch > 126)
      ch = ch == '\t' ? ' ' : '?';
    u32 f = inside ? fg : C_BG, b = inside ? bg : C_BG;
    const unsigned char *g = &msx[ch * 8];
    for (int y = 0; y < 8; y++) {
      unsigned bits = ui_bold_row(g[y]);
      u32 *px = &rowbuf[y * UI_SCREEN_COLS * 8 + cell * 8];
      for (int x = 0; x < 8; x++)
        px[x] = (bits & (0x80u >> x)) ? f : b;
    }
  }
  blit_row(UI_MARGIN_Y + lrow);
}

static void put_blank_screen_row(int prow) {
  for (size_t i = 0; i < sizeof(rowbuf) / sizeof(rowbuf[0]); i++)
    rowbuf[i] = C_BG;
  blit_row(prow);
}

static u32 tone_color(ui_tone_t t) {
  switch (t) {
  case UI_TONE_GOOD:
    return C_GOOD;
  case UI_TONE_WARN:
    return C_WARN;
  case UI_TONE_BAD:
    return C_BAD;
  default:
    return C_TEXT;
  }
}

static void put_rule(int lrow) {
  char line[UI_COLS + 1];
  memset(line, '-', UI_COLS);
  line[UI_COLS] = 0;
  put_row(lrow, line, C_RULE, C_BG);
}

/* Pad handling adapted from ps2-usbhdl src/ui.c (b681bc6). */

static char pad_buf[256] __attribute__((aligned(64)));
static int pad_lib_inited;
static int pad_open;
static int prev_pressed;

void ui_delay_ms(int ms) { DelayThread(ms * 1000); }

static int cur_row; /* ui_printf position (logical row) */

void ui_init(void) {
  init_scr();
  scr_setCursor(0);
  ui_clear();
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

int ui_wait_button_timeout(int ms) {
  for (int t = 0; t < ms; t += 50) {
    int b = ui_poll_button();
    if (b)
      return b;
    ui_delay_ms(50);
  }
  return 0;
}

int ui_wait_button(void) {
  if (!pad_open) {
    /* Without a pad, never "press" an accepting button: answering O
     * backs out of every menu and declines every confirmation, so no
     * HDD write can start unattended. */
    ui_delay_ms(3000);
    return UI_CIRCLE;
  }
  for (;;) {
    int n = ui_poll_button();
    if (n)
      return n;
    ui_delay_ms(30);
  }
}

void ui_clear(void) {
  for (int r = 0; r < UI_SCREEN_ROWS; r++)
    put_blank_screen_row(r);
  cur_row = 0;
}

/* Lines of text from the current ui_printf row on. */
void ui_printf(const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  for (const char *p = buf; *p && cur_row < UI_ROWS - 2;) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    char line[UI_ROW_LEN + 1];
    snprintf(line, sizeof(line), "%.*s", len, p);
    put_row(cur_row, line, tone_color(ui_line_tone(line)), C_BG);
    if (!nl)
      break;
    cur_row++;
    p = nl + 1;
  }
}

void ui_at(int row, const char *fmt, ...) {
  char buf[UI_ROW_LEN + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  put_row(row, buf, tone_color(ui_line_tone(buf)), C_BG);
}

void ui_header(const char *title, const char *status) {
  ui_clear();
  char bar[UI_ROW_LEN + 1];
  snprintf(bar, sizeof(bar), " UDPFS Game Installer  |  %s", title);
  put_row(0, bar, C_TITLE, C_BAR);
  if (status && status[0]) {
    snprintf(bar, sizeof(bar), " %s", status);
    put_row(1, bar, C_DIM, C_BG);
  }
  put_rule(2);
  cur_row = 3;
}

void ui_footer(const char *keys) {
  char buf[UI_ROW_LEN + 1];
  snprintf(buf, sizeof(buf), " %s", keys);
  put_rule(UI_ROWS - 2);
  put_row(UI_ROWS - 1, buf, C_ACCENT, C_BG);
}

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
        char line[UI_ROW_LEN + 1];
        if (i < n) {
          snprintf(line, sizeof(line), " %c %.*s", i == idx ? '>' : ' ', UI_COLS - 4, rows[i]);
          if (i == idx)
            put_row(4 + r, line, C_SEL_FG, C_SEL_BG);
          else
            put_row(4 + r, line, C_TEXT, C_BG);
        } else {
          put_row(4 + r, "", C_TEXT, C_BG);
        }
      }
      char more[UI_ROW_LEN + 1];
      snprintf(more, sizeof(more), "   %s%s", top > 0 ? "[more above] " : "",
               top + UI_LIST_ROWS < n ? "[more below]" : "");
      put_row(4 + UI_LIST_ROWS, more, C_DIM, C_BG);
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

void ui_text_view(const char *title, const char *text) {
  enum { MAXL = 256, ROWS = UI_ROWS - 5 };
  static const char *starts[MAXL];
  static int lens[MAXL];
  int n = 0;
  for (const char *p = text; *p && n < MAXL;) {
    const char *nl = strchr(p, '\n');
    starts[n] = p;
    lens[n] = nl ? (int)(nl - p) : (int)strlen(p);
    n++;
    p = nl ? nl + 1 : p + strlen(p);
  }
  int top = 0;
  ui_header(title, NULL);
  for (;;) {
    for (int r = 0; r < ROWS; r++) {
      int i = top + r;
      if (i < n)
        ui_at(3 + r, " %.*s", lens[i] > UI_COLS - 2 ? UI_COLS - 2 : lens[i], starts[i]);
      else
        ui_at(3 + r, "%s", "");
    }
    ui_footer(n > ROWS ? "[Up/Down] scroll  [L1/R1] page  [O]/[X] close"
                       : "[O]/[X] close");
    int b = ui_wait_button();
    if (b & (UI_CIRCLE | UI_CROSS | UI_TRIANGLE))
      return;
    int maxtop = n > ROWS ? n - ROWS : 0;
    if (b & UI_DOWN)
      top = top < maxtop ? top + 1 : maxtop;
    if (b & UI_UP)
      top = top > 0 ? top - 1 : 0;
    if (b & UI_R1)
      top = top + ROWS < maxtop ? top + ROWS : maxtop;
    if (b & UI_L1)
      top = top > ROWS ? top - ROWS : 0;
  }
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
