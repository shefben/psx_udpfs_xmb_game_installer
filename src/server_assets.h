#ifndef PSXI_SERVER_ASSETS_H
#define PSXI_SERVER_ASSETS_H

#include <stdint.h>

/* Decisions about files udpfsd prepares for the installer (pure). */

/* The server's OPL-Launcher copy is used only if it is a KELF and its
 * size and SHA-256 equal what the manifest header announced. */
int launcher_copy_valid(const void *data, uint32_t size, const char *sha_hex,
                        uint32_t want_size);

typedef enum { OPL_CFG_NONE = 0, OPL_CFG_COPY, OPL_CFG_KEEP } opl_cfg_action_t;

/* Copy the server's per-game OPL cfg only when OPL has none (the user's
 * own OPL settings always win). */
opl_cfg_action_t opl_cfg_decide(int offered, int dest_exists);

/* OPL's per-game config folder on the OPL partition mounted at pfs1:
 * "pfs1:CFG/" for "+..." partitions, else "pfs1:OPL/CFG/". */
const char *opl_cfg_dir(const char *opl_partition);

#endif
