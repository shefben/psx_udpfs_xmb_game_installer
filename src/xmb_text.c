#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "partname.h"
#include "xmb_text.h"

void xmb_sanitize_value(const char *in, char *out, size_t outsz) {
  size_t n = 0;
  if (outsz == 0)
    return;
  while (*in == ' ')
    in++;
  for (; *in && n < outsz - 1; in++) {
    unsigned char c = (unsigned char)*in;
    if (c < 0x20 || c == 0x7F)
      continue;
    out[n++] = (char)c;
  }
  while (n > 0 && out[n - 1] == ' ')
    n--;
  out[n] = 0;
}

static size_t render(char *out, size_t outsz, const char *title, const char *title_id,
                     const xmb_game_info_t *gi) {
  char t[256], id[64], rd[9] = "", dev[64] = "", pub[64] = "", gen[32] = "";
  xmb_sanitize_value(title, t, sizeof(t));
  xmb_sanitize_value(title_id, id, sizeof(id));
  if (gi) {
    xmb_sanitize_value(gi->release_date, rd, sizeof(rd));
    xmb_sanitize_value(gi->developer, dev, sizeof(dev));
    xmb_sanitize_value(gi->publisher, pub, sizeof(pub));
    xmb_sanitize_value(gi->genre, gen, sizeof(gen));
  }
  /* "key = value", or "key =" when empty (the template's form). */
#define KV(v) (v)[0] ? " " : "", (v)
  int n = snprintf(out, outsz,
                   "title = %s\r\n"
                   "title_id = %s\r\n"
                   "title_sub_id = 0\r\n"
                   "release_date =%s%s\r\n"
                   "developer_id =%s%s\r\n"
                   "publisher_id =%s%s\r\n"
                   "note =\r\n"
                   "content_web =\r\n"
                   "image_topviewflag = 0\r\n"
                   "image_type = 0\r\n"
                   "image_count = 1\r\n"
                   "image_viewsec = 600\r\n"
                   "copyright_viewflag = 0\r\n"
                   "copyright_imgcount = 0\r\n"
                   "genre =%s%s\r\n"
                   "parental_lock = 1\r\n"
                   "effective_date = 0\r\n"
                   "expire_date = 0\r\n"
                   "area = J\r\n"
                   "violence_flag = 0\r\n"
                   "content_type = 255\r\n"
                   "content_subtype = 0\r\n",
                   t, id, KV(rd), KV(dev), KV(pub), KV(gen));
#undef KV
  if (n < 0 || (size_t)n >= outsz) {
    if (outsz)
      out[0] = 0;
    return 0;
  }
  return (size_t)n;
}

/* Start of the value of the line "<key> = ..." (CRLF or LF), or NULL. */
static const char *find_key(const char *text, const char *key, const char **line_start) {
  size_t kl = strlen(key);
  for (const char *p = text; *p;) {
    if (!strncmp(p, key, kl) && p[kl] == ' ' && p[kl + 1] == '=') {
      if (line_start)
        *line_start = p;
      const char *v = p + kl + 2;
      while (*v == ' ')
        v++;
      return v;
    }
    const char *nl = strchr(p, '\n');
    if (!nl)
      break;
    p = nl + 1;
  }
  return NULL;
}

int xmb_info_sys_get(const char *text, const char *key, char *out, size_t outsz) {
  const char *v = find_key(text, key, NULL);
  if (!v || outsz == 0)
    return -1;
  size_t n = strcspn(v, "\r\n");
  if (n >= outsz)
    n = outsz - 1;
  memcpy(out, v, n);
  out[n] = 0;
  return 0;
}

size_t xmb_info_sys_retitle(const char *text, const char *title, char *out, size_t outsz) {
  char t[256];
  xmb_sanitize_value(title, t, sizeof(t));
  const char *line;
  const char *v = find_key(text, "title", &line);
  if (!t[0] || !v || outsz == 0)
    return 0;
  const char *rest = v + strcspn(v, "\r\n"); /* keeps this line's own ending */
  int n = snprintf(out, outsz, "%.*stitle = %s%s", (int)(line - text), text, t, rest);
  if (n < 0 || (size_t)n >= outsz) {
    out[0] = 0;
    return 0;
  }
  return (size_t)n;
}

size_t xmb_render_info_sys(char *out, size_t outsz, const char *title,
                           const char *title_id) {
  return render(out, outsz, title, title_id, NULL);
}

int xmb_game_info_parse(const char *text, xmb_game_info_t *gi) {
  memset(gi, 0, sizeof(*gi));
  static const struct {
    const char *key;
    size_t off, size;
  } F[] = {{"release_date", offsetof(xmb_game_info_t, release_date), sizeof(gi->release_date)},
           {"developer", offsetof(xmb_game_info_t, developer), sizeof(gi->developer)},
           {"publisher", offsetof(xmb_game_info_t, publisher), sizeof(gi->publisher)},
           {"genre", offsetof(xmb_game_info_t, genre), sizeof(gi->genre)}};
  int n = 0;
  for (const char *p = text; p && *p;) {
    const char *eol = p + strcspn(p, "\r\n");
    const char *eq = memchr(p, '=', (size_t)(eol - p));
    for (size_t i = 0; eq && i < sizeof(F) / sizeof(F[0]); i++) {
      size_t kl = strlen(F[i].key);
      if ((size_t)(eq - p) != kl || strncmp(p, F[i].key, kl))
        continue;
      char v[128];
      size_t vl = (size_t)(eol - eq - 1);
      if (vl >= sizeof(v))
        vl = sizeof(v) - 1;
      memcpy(v, eq + 1, vl);
      v[vl] = 0;
      char *dst = (char *)gi + F[i].off;
      xmb_sanitize_value(v, dst, F[i].size);
      if (i == 0 && (strlen(dst) != 8 || strspn(dst, "0123456789") != 8))
        dst[0] = 0; /* the XMB expects YYYYMMDD */
      n += dst[0] != 0;
    }
    p = eol + strspn(eol, "\r\n");
  }
  return n;
}

size_t xmb_game_info_sys_ex(char *out, size_t outsz, const char *title, const char *boot_id,
                            const xmb_game_info_t *gi) {
  char part_id[PART_ID_LEN + 1], title_id[48];
  if (boot_id_to_part_id(boot_id, part_id) < 0) {
    if (outsz)
      out[0] = 0;
    return 0;
  }
  snprintf(title_id, sizeof(title_id), "%s (%s)", part_id, region_label(boot_id));
  return render(out, outsz, title, title_id, gi);
}

size_t xmb_game_info_sys(char *out, size_t outsz, const char *title,
                         const char *boot_id) {
  return xmb_game_info_sys_ex(out, outsz, title, boot_id, NULL);
}
