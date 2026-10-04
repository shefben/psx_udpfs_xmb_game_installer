#ifndef PSXI_MANIFEST_H
#define PSXI_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "source.h"

/* udpfsd's prepared game list, udpfs:/.udpfsd/manifest.txt:
 *   udpfsd-manifest 1 [launcher=<sha256 hex>:<bytes>] [auto=0|1] [scanning=1]
 *   path \t status \t id \t title \t bytes \t disc \t layer1 \t jacket \t cfg
 * status is "ok" or "invalid:<reason>"; "-" marks an empty field. */

#define MANIFEST_MAX 512
#define MANIFEST_DIR "udpfs:/.udpfsd"

typedef struct {
  char path[SOURCE_PATH_MAX]; /* client path, starts with '/' */
  int ok;
  char reason[48];
  char id[16];
  char title[64];
  uint64_t bytes;
  int dvd;
  uint32_t layer1;
  char jacket[64]; /* relative to MANIFEST_DIR, "" if none */
  char cfg[64];    /* client path, "" if none */
} manifest_entry_t;

typedef struct {
  int version;
  int has_launcher;
  char launcher_sha[65];
  uint32_t launcher_size;
  int auto_install;
  int scanning; /* server still preparing: no entries yet */
  int n, n_bad;
  manifest_entry_t e[MANIFEST_MAX];
} manifest_t;

/* 0, or -1 for a missing/unknown header. Malformed entry lines are
 * counted in n_bad and skipped; extra (future) columns are ignored. */
int manifest_parse(const char *text, size_t len, manifest_t *m);

/* First "ok" entry with this game ID, or NULL. */
const manifest_entry_t *manifest_find_id(const manifest_t *m, const char *id);

/* Entry for a client path, with or without the "udpfs:" prefix. */
const manifest_entry_t *manifest_find_path(const manifest_t *m, const char *udpfs_path);

#ifdef _EE
extern manifest_t g_manifest;
extern int g_manifest_loaded;

/* Reads MANIFEST_DIR/manifest.txt; sets g_manifest_loaded. 0 on success. */
int manifest_load(void);
#endif

#endif
