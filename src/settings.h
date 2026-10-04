#ifndef PSXI_SETTINGS_H
#define PSXI_SETTINGS_H

#include <stddef.h>

#include "errors.h"

/* Persistent network settings (plan section 8). Stored as
 * <installer partition>/config/network.ini with `local_ip=a.b.c.d`. */

#define SETTINGS_DEFAULT_IP "192.168.1.10"

typedef struct {
  char local_ip[16];
  int using_default; /* 1 if the compiled default is in effect */
  int warning;       /* 1 if a config existed but was invalid */
} net_settings_t;

/* Strict IPv4 check: four decimal octets 0..255, no empty parts, no
 * leading '+'/'-'/spaces, at most 3 digits per octet. Rejects
 * 0.x.x.x, 127.x.x.x and >= 224.x.x.x (not usable as a host address,
 * and 0.0.0.0 is "unset" to ministack). */
int ip_is_valid(const char *ip);

/* Parse network.ini text. NULL text means "no file". On any problem
 * falls back to the default and sets `warning` (if a file existed). */
void settings_parse(const char *text, net_settings_t *out);

size_t settings_serialize(const net_settings_t *s, char *out, size_t outsz);

/* Octet editing helper for the UI: add `delta` to octet `idx` (0..3),
 * wrapping within 0..255. Returns 0, -1 if `ip` is malformed. */
int ip_adjust_octet(char ip[16], int idx, int delta);

#ifdef _EE
void settings_load(const char *path, net_settings_t *out);
inst_err_t settings_save(const char *path, const net_settings_t *s);
#endif

#endif
