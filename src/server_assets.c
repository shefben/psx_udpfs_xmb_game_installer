#include <string.h>

#include "hdl_header.h"
#include "server_assets.h"
#include "sha256.h"

int blob_matches(const void *data, uint32_t size, const char *sha_hex, uint32_t want_size) {
  char hex[65];
  if (!data || size == 0 || size != want_size || !sha_hex || strlen(sha_hex) != 64)
    return 0;
  sha256_hex(data, size, hex);
  return strcmp(hex, sha_hex) == 0;
}

int launcher_copy_valid(const void *data, uint32_t size, const char *sha_hex,
                        uint32_t want_size) {
  return kelf_looks_valid(data, size) && blob_matches(data, size, sha_hex, want_size);
}

opl_install_action_t opl_install_decide(int conf_malformed, int from_config, int part_exists,
                                        int elf_exists) {
  if (conf_malformed)
    return OPL_INSTALL_CANNOT;
  if (part_exists && elf_exists)
    return OPL_INSTALL_PRESENT;
  if (part_exists)
    return OPL_INSTALL_ADD_ELF;
  /* Only the default "+OPL" is created; a partition named in
   * conf_hdd.cfg that does not exist is the user's to sort out. */
  return from_config ? OPL_INSTALL_CANNOT : OPL_INSTALL_CREATE_PARTITION;
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
