#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "util.h"

/* Parse "a.b.c.d" strictly into octets. 0 on success. */
static int parse_octets(const char *ip, int oct[4]) {
  int idx = 0, digits = 0, val = 0;
  for (const char *p = ip;; p++) {
    if (*p >= '0' && *p <= '9') {
      if (++digits > 3)
        return -1;
      val = val * 10 + (*p - '0');
    } else if (*p == '.' || *p == 0) {
      if (digits == 0 || val > 255 || idx > 3)
        return -1;
      oct[idx++] = val;
      digits = val = 0;
      if (*p == 0)
        break;
    } else {
      return -1;
    }
  }
  return idx == 4 ? 0 : -1;
}

int ip_is_valid(const char *ip) {
  int o[4];
  if (!ip || parse_octets(ip, o) < 0)
    return 0;
  return o[0] != 0 && o[0] != 127 && o[0] < 224;
}

static void set_default(net_settings_t *out, int warning) {
  str_copy(out->local_ip, SETTINGS_DEFAULT_IP, sizeof(out->local_ip));
  out->using_default = 1;
  out->warning = warning;
}

void settings_parse(const char *text, net_settings_t *out) {
  memset(out, 0, sizeof(*out));
  out->dhcp = 1;
  out->fast_copy = 1;
  if (!text) {
    set_default(out, 0);
    return;
  }
  int have_ip = 0, have_mode = 0, bad = 0;
  const char *p = text;
  while (*p) {
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    char line[128];
    if (len < sizeof(line)) {
      memcpy(line, p, len);
      line[len] = 0;
      str_rtrim(line);
      char *eq = strchr(line, '=');
      if (line[0] != '#' && eq) {
        *eq = 0;
        str_rtrim(line);
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t')
          v++;
        if (strcmp(line, "local_ip") == 0) {
          if (ip_is_valid(v)) {
            str_copy(out->local_ip, v, sizeof(out->local_ip));
            have_ip = 1;
          } else {
            bad = 1;
          }
        } else if (strcmp(line, "console") == 0) {
          out->console = !strcmp(v, "psx1") ? CONSOLE_PSX1 :
                         !strcmp(v, "psx2") ? CONSOLE_PSX2 : CONSOLE_UNKNOWN;
          if (strcmp(v, "unknown") && out->console == CONSOLE_UNKNOWN)
            bad = 1;
        } else if (strcmp(line, "fast_copy") == 0) {
          out->fast_copy = strcmp(v, "0") != 0;
        } else if (strcmp(line, "ip_mode") == 0) {
          if (!strcmp(v, "dhcp") || !strcmp(v, "static")) {
            out->dhcp = v[0] == 'd';
            have_mode = 1;
          } else {
            bad = 1;
          }
        }
      }
    }
    p = eol ? eol + 1 : p + len;
  }
  if (!have_ip) {
    /* Without a usable address: default (as fallback for DHCP). Only a
     * file that says nothing usable at all is reported. */
    int dhcp = out->dhcp, fast = out->fast_copy;
    set_default(out, bad || !have_mode);
    out->dhcp = dhcp;
    out->fast_copy = fast;
  } else {
    out->warning = bad;
  }
}

void ip_format(uint32_t ip, char out[16]) {
  snprintf(out, 16, "%u.%u.%u.%u", (unsigned)(ip >> 24) & 255, (unsigned)(ip >> 16) & 255,
           (unsigned)(ip >> 8) & 255, (unsigned)ip & 255);
}

size_t settings_serialize(const net_settings_t *s, char *out, size_t outsz) {
  int n = snprintf(out, outsz, "local_ip=%s\nip_mode=%s\nfast_copy=%d\nconsole=%s\n", s->local_ip,
                   s->dhcp ? "dhcp" : "static", s->fast_copy ? 1 : 0,
                   console_name(s->console));
  if (n < 0 || (size_t)n >= outsz)
    return 0;
  return (size_t)n;
}

int ip_adjust_octet(char ip[16], int idx, int delta) {
  int o[4];
  if (idx < 0 || idx > 3 || parse_octets(ip, o) < 0)
    return -1;
  o[idx] = ((o[idx] + delta) % 256 + 256) % 256;
  snprintf(ip, 16, "%d.%d.%d.%d", o[0], o[1], o[2], o[3]);
  return 0;
}

#ifdef _EE
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

void settings_load(const char *path, net_settings_t *out) {
  char buf[256];
  int fd = fileXioOpen(path, FIO_O_RDONLY);
  if (fd < 0) {
    settings_parse(NULL, out);
    return;
  }
  int r = fileXioRead(fd, buf, sizeof(buf) - 1);
  fileXioClose(fd);
  if (r < 0)
    r = 0;
  buf[r] = 0;
  settings_parse(buf, out);
}

inst_err_t settings_save(const char *path, const net_settings_t *s) {
  char buf[128];
  size_t n = settings_serialize(s, buf, sizeof(buf));
  if (!n) return ERR_JOURNAL;
  int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
  if (fd < 0)
    return ERR_JOURNAL;
  int w = fileXioWrite(fd, buf, (int)n);
  fileXioClose(fd);
  return w == (int)n ? ERR_OK : ERR_JOURNAL;
}
#endif
