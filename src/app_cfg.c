#include <stdio.h>
#include <string.h>

#include "app_cfg.h"

static int is_space(char c) { return c == ' ' || c == '\t'; }

/* Copy [b, e) trimmed of spaces into out (cut to outsz - 1). */
static void trim_copy(const char *b, const char *e, char *out, size_t outsz) {
  while (b < e && is_space(*b))
    b++;
  while (e > b && is_space(e[-1]))
    e--;
  size_t n = (size_t)(e - b);
  if (n >= outsz)
    n = outsz - 1;
  memcpy(out, b, n);
  out[n] = 0;
}

int app_path_ok(const char *p) {
  if (!p || !p[0] || p[0] == '/' || strchr(p, ':') || strchr(p, '\\') || strstr(p, ".."))
    return 0;
  /* the channel's own files stay the installer's */
  if (!strcmp(p, APP_CFG_FILE) || !strcmp(p, "EXECUTE.KELF") || !strncmp(p, "res/", 4))
    return 0;
  return 1;
}

int app_cfg_parse(const char *text, size_t len, app_cfg_t *out) {
  memset(out, 0, sizeof(*out));
  const char *p = text, *end = text + len;
  while (p < end) {
    const char *eol = memchr(p, '\n', (size_t)(end - p));
    const char *le = eol ? eol : end;
    if (le > p && le[-1] == '\r')
      le--;
    const char *eq = memchr(p, '=', (size_t)(le - p));
    const char *s = p;
    while (s < le && is_space(*s))
      s++;
    if (eq && s < le && *s != '#') {
      char key[8], val[APP_PATH_MAX];
      trim_copy(p, eq, key, sizeof(key));
      trim_copy(eq + 1, le, val, sizeof(val));
      if (!strcmp(key, "boot"))
        memcpy(out->boot, val, sizeof(val));
      else if (!strcmp(key, "arg") && out->nargs < APP_ARGS_MAX)
        memcpy(out->args[out->nargs++], val, sizeof(val));
    }
    p = eol ? eol + 1 : end;
  }
  return app_path_ok(out->boot) ? 0 : -1;
}

size_t app_cfg_render(const app_cfg_t *c, char *out, size_t outsz) {
  int n = snprintf(out, outsz,
                   "# App channel made by UDPFS Game Installer.\n"
                   "# boot = the ELF in this partition; arg = one argument per line.\n"
                   "boot = %s\n",
                   c->boot);
  if (n < 0 || (size_t)n >= outsz)
    return 0;
  size_t used = (size_t)n;
  for (int i = 0; i < c->nargs && i < APP_ARGS_MAX; i++) {
    n = snprintf(out + used, outsz - used, "arg = %s\n", c->args[i]);
    if (n < 0 || (size_t)n >= outsz - used)
      return 0;
    used += (size_t)n;
  }
  return used;
}
