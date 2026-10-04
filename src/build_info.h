#ifndef PSXI_BUILD_INFO_H
#define PSXI_BUILD_INFO_H

/* Generated per build variant by tools/gen-build-info.sh into
 * build/<variant>/build_info.c. Expected SHA-256 of every embedded
 * blob, so the diagnostics page can check them at runtime. */

typedef struct {
  const char *name;   /* bin2c symbol, e.g. "ps2hdd_hdl_irx" */
  const char *sha256; /* lowercase hex */
  unsigned int size;
} build_blob_t;

extern const char build_id[];      /* git describe --always --dirty */
extern const char build_variant[]; /* "app", "bootstrap", "dev" */
extern const char build_date[];
extern const build_blob_t build_blobs[];
extern const int build_nblobs;

/* Expected entry for a blob, or NULL. */
const build_blob_t *build_blob(const char *name);

#endif
