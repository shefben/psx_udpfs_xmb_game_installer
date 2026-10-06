#include <stdio.h>
#include <string.h>

#include "apps.h"

/* NNNNN of a "PP.APPS-NNNNN.." name, or 0. */
static unsigned app_number(const char *name) {
  if (!partition_is_app(name) || strncmp(name, "PP.APPS-", 8))
    return 0;
  unsigned v = 0;
  for (int i = 8; i < 13; i++)
    v = v * 10 + (unsigned)(name[i] - '0');
  return v;
}

int app_partition_name(const char *title, const char *const *names, int n,
                       char out[APA_NAME_MAX + 1]) {
  unsigned num = 1;
  for (;; num++) {
    if (num > 99999)
      return -1;
    int used = 0;
    for (int i = 0; i < n && !used; i++)
      used = names[i] && app_number(names[i]) == num;
    if (!used)
      break;
  }
  char t[APA_NAME_MAX - 15 + 1]; /* the title after "PP.APPS-NNNNN.." */
  size_t len = 0, room = sizeof(t) - 1;
  for (const char *s = title ? title : ""; *s && len < room; s++) {
    char c = *s;
    if (c >= 'a' && c <= 'z')
      c = (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      t[len++] = c;
    else if (len && t[len - 1] != '_')
      t[len++] = '_';
  }
  while (len && t[len - 1] == '_')
    len--;
  t[len] = 0;
  snprintf(out, APA_NAME_MAX + 1, "PP.APPS-%05u..%s", num % 100000u, len ? t : "APP");
  return 0;
}

const char *app_size_str(uint64_t content_mb, uint32_t *size_mb) {
  static const struct {
    uint32_t mb;
    const char *s;
  } SIZES[] = {{128, "128M"}, {256, "256M"}, {512, "512M"}, {1024, "1G"}, {2048, "2G"}};
  for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++)
    if (content_mb + 16 <= SIZES[i].mb) {
      if (size_mb)
        *size_mb = SIZES[i].mb;
      return SIZES[i].s;
    }
  return NULL;
}

void app_title_from_name(const char *name, char *out, size_t outsz) {
  size_t n = strlen(name);
  const char *dot = strrchr(name, '.');
  if (dot && (!strcmp(dot, ".elf") || !strcmp(dot, ".ELF") || !strcmp(dot, ".Elf")))
    n = (size_t)(dot - name);
  if (n >= outsz)
    n = outsz - 1;
  size_t j = 0;
  for (size_t i = 0; i < n; i++)
    out[j++] = name[i] == '_' ? ' ' : name[i];
  out[j] = 0;
  if (!j)
    snprintf(out, outsz, "App");
}
