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

char xmb_area_letter(const char *boot_id) {
  if (!boot_id || strlen(boot_id) < 4)
    return 'X';
  switch (boot_id[2]) {
  case 'U':
    return 'U';
  case 'E':
    return 'E';
  case 'P':
    return 'J';
  case 'A':
    return 'A';
  case 'C':
    return 'C';
  case 'K':
    return 'K';
  }
  return 'X';
}

static size_t render(char *out, size_t outsz, const char *title, const char *title_id,
                     char area, const xmb_game_info_t *gi, const char *today) {
  char t[256], id[64], rd[9] = "", dev[64] = "", pub[64] = "", gen[32] = "";
  xmb_sanitize_value(title, t, sizeof(t));
  xmb_sanitize_value(title_id, id, sizeof(id));
  if (gi) {
    xmb_sanitize_value(gi->release_date, rd, sizeof(rd));
    xmb_sanitize_value(gi->developer, dev, sizeof(dev));
    xmb_sanitize_value(gi->publisher, pub, sizeof(pub));
    xmb_sanitize_value(gi->genre, gen, sizeof(gen));
  }
  /* No release date known: the install date (YYYYMMDD) instead. */
  if (!rd[0] && today && strlen(today) == 8 && strspn(today, "0123456789") == 8)
    memcpy(rd, today, 9);
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
                   "copyright_imgcount = 1\r\n"
                   "genre =%s%s\r\n"
                   "parental_lock = 1\r\n"
                   "effective_date = 0\r\n"
                   "expire_date = 0\r\n"
                   "area = %c\r\n"
                   "violence_flag = 0\r\n"
                   "content_type = 255\r\n"
                   "content_subtype = 0\r\n",
                   t, id, KV(rd), KV(dev), KV(pub), KV(gen), area);
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
                           const char *title_id, const char *today) {
  return render(out, outsz, title, title_id, 'X', NULL, today);
}

size_t xmb_render_icon_sys(char *out, size_t outsz, const char *title0, const char *title1) {
  char t0[65], t1[33];
  xmb_sanitize_value(title0, t0, sizeof(t0));
  xmb_sanitize_value(title1, t1, sizeof(t1));
  int n = snprintf(out, outsz,
                   "PS2X\n"
                   "title0 = %s\n"
                   "title1 = %s\n"
                   "bgcola = 64\n"
                   "bgcol0 = 22,47,92\n"
                   "bgcol1 = 3,10,28\n"
                   "bgcol2 = 3,10,28\n"
                   "bgcol3 = 22,47,92\n"
                   "lightdir0 = 0.5000,0.5000,0.5000\n"
                   "lightdir1 = 0.0000,-0.4000,-1.0000\n"
                   "lightdir2 = 0.5000,-0.5000,0.5000\n"
                   "lightcolamb = 31,31,31\n"
                   "lightcol0 = 62,62,55\n"
                   "lightcol1 = 33,42,64\n"
                   "lightcol2 = 18,18,49\n"
                   "uninstallmes0 = This will delete the game.\n"
                   "uninstallmes1 =\n"
                   "uninstallmes2 =\n",
                   t0, t1);
  if (n < 0 || (size_t)n >= outsz) {
    if (outsz)
      out[0] = 0;
    return 0;
  }
  return (size_t)n;
}

size_t xmb_render_man_xml(char *out, size_t outsz, const char *title) {
  char t[256], esc[600];
  xmb_sanitize_value(title, t, sizeof(t));
  size_t e = 0;
  for (const char *p = t; *p && e < sizeof(esc) - 7; p++) {
    const char *r = *p == '&'   ? "&amp;"
                    : *p == '<' ? "&lt;"
                    : *p == '>' ? "&gt;"
                    : *p == '"' ? "&quot;"
                                : NULL;
    if (r) {
      memcpy(esc + e, r, strlen(r));
      e += strlen(r);
    } else {
      esc[e++] = *p;
    }
  }
  esc[e] = 0;
  int n = snprintf(out, outsz,
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                   "\r\n"
                   "<MANUAL version=\"1.0\">\r\n"
                   "\r\n"
                   "\t<IMG id=\"bg\" src=\"./image/0.png\" />\r\n"
                   "\r\n"
                   "\t<MENUGROUP id=\"TOP\">\r\n"
                   "\t\t<TITLE id=\"TOP-TITLE\" label=\"%s\" />\r\n"
                   "\t\t<ITEM id=\"M00\" label=\"Screenshots\"\tpage=\"PIC0000\" />\r\n"
                   "\t</MENUGROUP>\r\n"
                   "\r\n"
                   "\t<PAGEGROUP>\r\n"
                   "\t\t<PAGE id=\"PIC0000\" src=\"./image/1.png\" retitem=\"M00\" retgroup=\"TOP\" />\r\n"
                   "\t\t<PAGE id=\"PIC0000\" src=\"./image/2.png\" retitem=\"M00\" retgroup=\"TOP\" />\r\n"
                   "\t</PAGEGROUP>\r\n"
                   "</MANUAL>\r\n",
                   esc);
  if (n < 0 || (size_t)n >= outsz) {
    if (outsz)
      out[0] = 0;
    return 0;
  }
  return (size_t)n;
}

int xmb_date_str(int year, int month, int day, char out[9]) {
  static const int DAYS[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 ||
      day > DAYS[month - 1] || (month == 2 && day == 29 && year % 4))
    return -1;
  int v[8] = {year / 1000, year / 100 % 10, year / 10 % 10, year % 10,
              month / 10,  month % 10,      day / 10,       day % 10};
  for (int i = 0; i < 8; i++)
    out[i] = (char)('0' + v[i]);
  out[8] = 0;
  return 0;
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
                            const xmb_game_info_t *gi, const char *today) {
  char part_id[PART_ID_LEN + 1];
  if (boot_id_to_part_id(boot_id, part_id) < 0) {
    if (outsz)
      out[0] = 0;
    return 0;
  }
  return render(out, outsz, title, part_id, xmb_area_letter(boot_id), gi, today);
}

size_t xmb_game_info_sys(char *out, size_t outsz, const char *title,
                         const char *boot_id) {
  return xmb_game_info_sys_ex(out, outsz, title, boot_id, NULL, NULL);
}
