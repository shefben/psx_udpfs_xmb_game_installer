#include <string.h>

#include "hdl_header.h"
#include "server_assets.h"
#include "sha256.h"

int launcher_copy_valid(const void *data, uint32_t size, const char *sha_hex,
                        uint32_t want_size) {
  char hex[65];
  if (!data || size == 0 || size != want_size || !sha_hex || strlen(sha_hex) != 64 ||
      !kelf_looks_valid(data, size))
    return 0;
  sha256_hex(data, size, hex);
  return strcmp(hex, sha_hex) == 0;
}

opl_cfg_action_t opl_cfg_decide(int offered, int dest_exists) {
  if (!offered)
    return OPL_CFG_NONE;
  return dest_exists ? OPL_CFG_KEEP : OPL_CFG_COPY;
}

const char *opl_cfg_dir(const char *opl_partition) {
  /* OPL: gHDDPrefix is "pfs0:" for "+" partitions, else "pfs0:OPL/";
   * per-game configs live in <prefix>CFG/. The installer mounts the
   * OPL partition at pfs1:. */
  return opl_partition[0] == '+' ? "pfs1:CFG/" : "pfs1:OPL/CFG/";
}
