#ifndef PSXI_DISC_CDVD_H
#define PSXI_DISC_CDVD_H
#include "source_disc.h"
/* Lazy CDVD initialization; failure leaves HDD/network installation usable. */
int disc_cdvd_prepare(void *ctx, disc_info_t *info);
void source_cdvd_init(GameSource *src, disc_src_t *d);
#endif
