#include <kernel.h>
#include <delaythread.h>
#include <libcdvd.h>
#include <string.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <cdvd-ioctl.h>
#include "app_state.h"
#include "disc_cdvd.h"
#include "iop_boot.h"

static disc_info_t current;
static uint8_t data[DISC_CACHE_SECTORS * 2352] __attribute__((aligned(64)));
static int from_bcd(unsigned v) { return (int)((v >> 4) * 10 + (v & 15)); }
static uint32_t track_start(const uint8_t *t) {
  return (uint32_t)((from_bcd(t[7]) * 60 + from_bcd(t[8])) * 75 + from_bcd(t[9]) - 150);
}
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int read_cooked(uint32_t lba, uint32_t n, void *out) {
  sceCdRMode mode = {16, SCECdSpinNom, SCECdSecS2048, 0};
  if (!sceCdRead(lba, n, out, &mode)) return -5;
  sceCdSync(0);
  int e = sceCdGetError();
  return e == SCECdErNO ? 0 : -e;
}
int disc_cdvd_prepare(void *ctx, disc_info_t *info) {
  (void)ctx;
  if (iop_load_disc(&g_app.iop) < 0) return -19;
  /* Poll detection for at most ten seconds, without asking a no-disc
   * drive to block in sceCdDiskReady(0). */
  int type = sceCdGetDiskType();
  for (int i = 0; type > 0 && type < SCECdPSCD && i < 100; i++) {
    DelayThread(100000);
    type = sceCdGetDiskType();
  }
  memset(info, 0, sizeof(*info));
  info->ps1 = type == SCECdPSCD || type == SCECdPSCDDA;
  info->dvd = type == SCECdPS2DVD;
  if (!info->ps1 && !info->dvd && type != SCECdPS2CD && type != SCECdPS2CDDA) return -19;
  if (!info->dvd) {
    if (!sceCdGetToc(data)) return -5;
    sceCdSync(0);
    if (sceCdGetError() != SCECdErNO) return -5;
    memcpy(info->toc, data, sizeof(info->toc));
    if (disc_toc_sectors(info->toc, &info->sectors) < 0) return -22;
  } else {
    if (read_cooked(16, 1, data) < 0 || memcmp(data, "\x01" "CD001", 6)) return -5;
    uint32_t blocks = le32(data + 80);
    if (blocks <= 16) return -22;
    info->sectors = blocks;
    /* With the embedded current CDVDMAN, dual-layer information is
     * supported. Refuse unknown layer geometry rather than truncate a
     * DVD9 to layer zero and report a successful installation. */
    unsigned layer1 = 0;
    /* The SDK declares sceCdReadDvdDualInfo in its shared header, but
     * implements it on the IOP only. CDVDMAN's devctl exposes its layer
     * start to EE callers (zero means a single-layer disc). */
    if (fileXioDevctl("cdrom0:", CDIOC_READDVDDUALINFO, NULL, 0, &layer1, sizeof(layer1)) < 0) return -5;
    if (layer1) {
      if (!layer1 || layer1 > UINT32_MAX - 16 || read_cooked(layer1 + 16, 1, data) < 0 ||
          memcmp(data, "\x01" "CD001", 6)) return -5;
      uint32_t layer_blocks = le32(data + 80);
      if (layer_blocks <= 16 || layer_blocks > UINT32_MAX - layer1) return -22;
      info->layer1_start = layer1;
      info->sectors = layer1 + layer_blocks;
    }
  }
  current = *info;
  return 0;
}
static int read_raw(void *ctx, uint32_t lba, uint32_t n, int raw, void *out) {
  (void)ctx;
  if (!raw) return read_cooked(lba, n, out);
  uint8_t *p = out;
  while (n) {
    int tracks = from_bcd(current.toc[17]), index = 0;
    for (int i = 1; i < tracks; i++)
      if (lba >= track_start(current.toc + 30 + i * 10)) index = i;
    int audio = !(current.toc[30 + index * 10] & 0x40);
    uint32_t end = index + 1 < tracks ? track_start(current.toc + 40 + index * 10) : current.sectors;
    uint32_t take = n < end - lba ? n : end - lba;
    if (!take || take > DISC_CACHE_SECTORS) return -22;
    sceCdRMode mode = {16, SCECdSpinNom, audio ? SCECdSecS2352 : SCECdSecS2340, 0};
    int r = audio ? sceCdReadCDDA(lba, take, data, &mode) : sceCdRead(lba, take, data, &mode);
    if (!r) return -5;
    sceCdSync(0);
    int e = sceCdGetError();
    if (e != SCECdErNO) return -e;
    for (uint32_t i = 0; i < take; i++, p += 2352) {
      if (audio) memcpy(p, data + i * 2352, 2352);
      else {
        p[0] = p[11] = 0;
        memset(p + 1, 0xff, 10);
        memcpy(p + 12, data + i * 2340, 2340);
      }
    }
    lba += take;
    n -= take;
  }
  return 0;
}
void source_cdvd_init(GameSource *src, disc_src_t *d) {
  source_disc_init(src, d, disc_cdvd_prepare, read_raw, NULL);
}
