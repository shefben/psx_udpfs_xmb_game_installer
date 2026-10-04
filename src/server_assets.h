#ifndef PSXI_SERVER_ASSETS_H
#define PSXI_SERVER_ASSETS_H

#include <stdint.h>

/* Decisions about files udpfsd prepares for the installer (pure). */

/* The server's OPL-Launcher copy is used only if it is a KELF and its
 * size and SHA-256 equal what the manifest header announced. */
int launcher_copy_valid(const void *data, uint32_t size, const char *sha_hex,
                        uint32_t want_size);

/* data is exactly want_size bytes with SHA-256 sha_hex. */
int blob_matches(const void *data, uint32_t size, const char *sha_hex, uint32_t want_size);

/* OPL runtime install from the server (OPL itself missing on the HDD):
 * conf_malformed / from_config come from opl_resolve_from_conf(),
 * part_exists / elf_exists describe the resolved OPL partition. */
typedef enum {
  OPL_INSTALL_PRESENT = 0,      /* nothing to do */
  OPL_INSTALL_CREATE_PARTITION, /* default +OPL missing: create it like OPL does */
  OPL_INSTALL_ADD_ELF,          /* partition exists, OPNPS2LD.ELF missing */
  OPL_INSTALL_CANNOT,           /* malformed conf_hdd.cfg, or it names a missing partition */
} opl_install_action_t;
opl_install_action_t opl_install_decide(int conf_malformed, int from_config, int part_exists,
                                        int elf_exists);

typedef enum { OPL_CFG_NONE = 0, OPL_CFG_COPY, OPL_CFG_KEEP } opl_cfg_action_t;

/* Copy the server's per-game OPL cfg only when OPL has none (the user's
 * own OPL settings always win). */
opl_cfg_action_t opl_cfg_decide(int offered, int dest_exists);

/* OPL's per-game config folder on the OPL partition mounted at pfs1:
 * "pfs1:CFG/" for "+..." partitions, else "pfs1:OPL/CFG/". */
const char *opl_cfg_dir(const char *opl_partition);

#endif
