#ifndef PSXI_SOURCE_UDPFS_H
#define PSXI_SOURCE_UDPFS_H

#include "source.h"

/* GameSource over the `udpfs:` iomanX device (Neutrino udpfs_ioman).
 * Opens exactly the path given — for ZSO that is udpfsd's virtual
 * "<name>.zso.iso", which serves decompressed ISO bytes. */

typedef struct {
  int fd;
  int64_t pos;  /* tracked logical position */
  int64_t size; /* cached logical size */
} udpfs_src_t;

void source_udpfs_init(GameSource *src, udpfs_src_t *u);

#endif
