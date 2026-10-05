#include <stdio.h>
#include <string.h>

#include "manifest.h"
#include "partname.h"
#include "util.h"

static int is_hex64(const char *s, size_t n) {
  if (n != 64)
    return 0;
  for (size_t i = 0; i < n; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return 0;
  return 1;
}

/* "<64 lowercase hex>:<bytes>" (n chars at v) -> sha/size; 1 if valid.
 * A malformed value is never trusted. */
static int parse_sha_size(const char *v, size_t n, char sha[65], uint32_t *size) {
  const char *colon = memchr(v, ':', n);
  if (!colon || !is_hex64(v, (size_t)(colon - v)))
    return 0;
  char num[24];
  size_t nl = n - (size_t)(colon + 1 - v);
  uint64_t sz;
  if (nl == 0 || nl >= sizeof(num))
    return 0;
  memcpy(num, colon + 1, nl);
  num[nl] = 0;
  if (parse_u64(num, &sz) != 0 || sz == 0 || sz >= 0xFFFFFFFFull)
    return 0;
  memcpy(sha, v, 64);
  sha[64] = 0;
  *size = (uint32_t)sz;
  return 1;
}

static int parse_header(const char *line, manifest_t *m) {
  if (strncmp(line, "udpfsd-manifest ", 16) != 0)
    return -1;
  const char *p = line + 16;
  if (p[0] != '1' || (p[1] != 0 && p[1] != ' '))
    return -1;
  m->version = 1;
  for (p++; *p;) {
    while (*p == ' ')
      p++;
    const char *end = strchr(p, ' ');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n > 9 && !strncmp(p, "launcher=", 9)) {
      m->has_launcher = parse_sha_size(p + 9, n - 9, m->launcher_sha, &m->launcher_size);
    } else if (n > 4 && !strncmp(p, "opl=", 4)) {
      m->has_opl = parse_sha_size(p + 4, n - 4, m->opl_sha, &m->opl_size);
    } else if (n == 6 && !strncmp(p, "auto=", 5)) {
      m->auto_install = p[5] == '1';
    } else if (n == 10 && !strncmp(p, "poweroff=", 9)) {
      m->power_off = p[9] == '1';
    } else if (n == 10 && !strncmp(p, "scanning=", 9)) {
      m->scanning = p[9] == '1';
    }
    p += n;
  }
  return 0;
}

/* Split `line` (modified in place) on tabs into at most max fields. */
static int split_tabs(char *line, char **f, int max) {
  int n = 0;
  f[n++] = line;
  for (char *p = line; *p && n < max; p++)
    if (*p == '\t') {
      *p = 0;
      f[n++] = p + 1;
    }
  for (char *p = f[n - 1]; *p; p++) /* drop future extra columns */
    if (*p == '\t') {
      *p = 0;
      break;
    }
  return n;
}

static int parse_entry(char *line, manifest_entry_t *e) {
  char *f[9];
  /* room for the "udpfs:" prefix the installer adds */
  if (split_tabs(line, f, 9) != 9 || f[0][0] != '/' || strlen(f[0]) + 6 >= sizeof(e->path))
    return -1;
  memset(e, 0, sizeof(*e));
  str_copy(e->path, f[0], sizeof(e->path));
  if (parse_u64(f[4], &e->bytes) < 0)
    return -1;
  if (!strncmp(f[1], "invalid:", 8)) {
    str_copy(e->reason, f[1] + 8, sizeof(e->reason));
    return 0;
  }
  if (strcmp(f[1], "ok") || !boot_id_is_valid(f[2]))
    return -1;
  e->ok = 1;
  str_copy(e->id, f[2], sizeof(e->id));
  str_copy(e->title, f[3], sizeof(e->title));
  if (!strcmp(f[5], "DVD"))
    e->dvd = 1;
  else if (strcmp(f[5], "CD"))
    return -1;
  uint64_t l1;
  if (parse_u64(f[6], &l1) < 0 || l1 > 0xFFFFFFFFull)
    return -1;
  e->layer1 = (uint32_t)l1;
  if (strcmp(f[7], "-")) {
    if (strstr(f[7], "..") || f[7][0] == '/' || strlen(f[7]) >= sizeof(e->jacket))
      return -1;
    str_copy(e->jacket, f[7], sizeof(e->jacket));
  }
  if (strcmp(f[8], "-")) {
    if (strstr(f[8], "..") || f[8][0] != '/' || strlen(f[8]) >= sizeof(e->cfg))
      return -1;
    str_copy(e->cfg, f[8], sizeof(e->cfg));
  }
  return 0;
}

int manifest_parse(const char *text, size_t len, manifest_t *m) {
  memset(m, 0, sizeof(*m));
  static char line[1024];
  size_t pos = 0;
  int first = 1;
  while (pos < len) {
    size_t end = pos;
    while (end < len && text[end] != '\n')
      end++;
    size_t n = end - pos;
    if (n && text[pos + n - 1] == '\r')
      n--;
    if (n < sizeof(line)) {
      memcpy(line, text + pos, n);
      line[n] = 0;
      if (first) {
        if (parse_header(line, m) < 0)
          return -1;
        first = 0;
      } else if (n > 0) {
        if (m->n < MANIFEST_MAX && parse_entry(line, &m->e[m->n]) == 0)
          m->n++;
        else
          m->n_bad++;
      }
    } else {
      if (first)
        return -1;
      m->n_bad++;
    }
    pos = end + 1;
  }
  return first ? -1 : 0;
}

const manifest_entry_t *manifest_find_id(const manifest_t *m, const char *id) {
  for (int i = 0; i < m->n; i++)
    if (m->e[i].ok && !strcmp(m->e[i].id, id))
      return &m->e[i];
  return NULL;
}

const manifest_entry_t *manifest_find_path(const manifest_t *m, const char *p) {
  if (!strncmp(p, "udpfs:", 6))
    p += 6;
  for (int i = 0; i < m->n; i++)
    if (!strcmp(m->e[i].path, p))
      return &m->e[i];
  return NULL;
}

#ifdef _EE
#include <malloc.h>

#include "hdd_partitions.h"

manifest_t g_manifest;
int g_manifest_loaded;

int manifest_load(void) {
  void *buf = NULL;
  g_manifest_loaded = 0;
  int n = file_load(MANIFEST_DIR "/manifest.txt", &buf, 512 * 1024);
  if (n <= 0)
    return -1;
  int r = manifest_parse(buf, (size_t)n, &g_manifest);
  free(buf);
  g_manifest_loaded = r == 0;
  return r;
}
#endif
