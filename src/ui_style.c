#include <string.h>

#include "ui_style.h"

uint8_t ui_bold_row(uint8_t bits) { return (uint8_t)(bits | (bits >> 1)); }

static int word_is(const char *p, const char *w) {
  size_t n = strlen(w);
  if (strncmp(p, w, n) != 0)
    return 0;
  char c = p[n];
  return c == 0 || c == ' ' || c == ':' || c == '\t';
}

ui_tone_t ui_line_tone(const char *line) {
  const char *p = line;
  while (*p == ' ')
    p++;
  if (word_is(p, "FAIL") || word_is(p, "FAILED") || word_is(p, "ERROR"))
    return UI_TONE_BAD;
  if (word_is(p, "WARN") || word_is(p, "WARNING"))
    return UI_TONE_WARN;
  if (word_is(p, "PASS") || word_is(p, "OK") || word_is(p, "installed"))
    return UI_TONE_GOOD;
  return UI_TONE_NORMAL;
}
