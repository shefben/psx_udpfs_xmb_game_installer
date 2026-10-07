/* Fake disc drive for source_cdvd.c: a PS2 disc of fake_cd_sectors
 * whose PVD (sector 16) says fake_cd_l0 blocks; with fake_cd_l1 a second
 * PVD sits at sector fake_cd_l0 (DVD-9 layer 1). Every other sector byte
 * is fake_cd_byte(). Reads complete after a few sceCdSync polls and land
 * in the buffer only then. */
#include <string.h>

#include "fake/libcdvd.h"
#include "fake/sifrpc.h"

int fake_cd_type = 0x14; /* SCECdPS2DVD */
uint32_t fake_cd_sectors, fake_cd_l0, fake_cd_l1;
int fake_cd_violations, fake_cd_fail_lsn = -1, fake_cd_reads;

static int pending, polls, err;
static uint8_t *dst;
static uint32_t p_lsn, p_n;

uint8_t fake_cd_byte(uint64_t off) { return (uint8_t)(off * 7 + (off >> 11)); }

static void pvd(uint8_t *s, uint32_t blocks) {
  s[0] = 1;
  memcpy(s + 1, "CD001", 5);
  s[80] = (uint8_t)blocks;
  s[81] = (uint8_t)(blocks >> 8);
  s[82] = (uint8_t)(blocks >> 16);
  s[83] = (uint8_t)(blocks >> 24);
}

void fake_cd_sector(uint32_t lsn, uint8_t *s) {
  if (lsn >= fake_cd_sectors) {
    memset(s, 0, 2048);
    return;
  }
  for (int i = 0; i < 2048; i++)
    s[i] = fake_cd_byte((uint64_t)lsn * 2048 + (uint64_t)i);
  if (lsn == 16)
    pvd(s, fake_cd_l0);
  if (fake_cd_l1 && lsn == fake_cd_l0)
    pvd(s, fake_cd_l1);
}

int SifBindRpc(SifRpcClientData_t *cd, int rpc, int mode) {
  (void)rpc;
  (void)mode;
  cd->server = cd;
  return 0;
}
void nopdelay(void) {}

/* cdvdfsv S-command 39 (sceCdReadDvdDualInfo). */
int fake_cd_dual, fake_cd_dual_fail, fake_cd_busy_once;
uint32_t fake_cd_l1start;
int SifCallRpc(SifRpcClientData_t *cd, int fno, int mode, void *send, int ssize, void *recv,
               int rsize, void *end, void *end_arg) {
  (void)cd;
  (void)mode;
  (void)send;
  (void)ssize;
  (void)end;
  (void)end_arg;
  if (fno != 39 || rsize < 12)
    return -1;
  int *o = recv;
  o[0] = fake_cd_dual_fail ? 0 : 1;
  o[1] = fake_cd_dual;
  o[2] = (int)fake_cd_l1start;
  return 0;
}
void ui_delay_ms(int ms) { (void)ms; }

int sceCdInit(int mode) {
  (void)mode;
  return 1;
}
int sceCdGetDiskType(void) { return fake_cd_type; }
int sceCdDiskReady(int mode) {
  (void)mode;
  return SCECdComplete;
}
int sceCdRead(u32 lsn, u32 n, void *buf, sceCdRMode *mode) {
  (void)mode;
  if (pending)
    fake_cd_violations++;
  if (fake_cd_busy_once) { /* the drive refuses the command once */
    fake_cd_busy_once = 0;
    return 0;
  }
  fake_cd_reads++;
  pending = 1;
  polls = 2;
  dst = buf;
  p_lsn = lsn;
  p_n = n;
  err = fake_cd_fail_lsn >= 0 && (uint32_t)fake_cd_fail_lsn >= lsn &&
        (uint32_t)fake_cd_fail_lsn < lsn + n;
  if (err)
    fake_cd_fail_lsn = -1; /* fails once */
  memset(buf, 0xEE, (size_t)n * 2048);
  return 1;
}
int sceCdSync(int mode) {
  if (!pending)
    return 0;
  if (mode == 1 && polls-- > 0)
    return 1;
  for (uint32_t i = 0; i < p_n && !err; i++)
    fake_cd_sector(p_lsn + i, dst + (size_t)i * 2048);
  pending = 0;
  return 0;
}
int sceCdGetError(void) { return err ? 0x30 : 0; }
int sceCdTrayReq(int param, u32 *t) {
  (void)param;
  (void)t;
  return 1;
}
int sceCdStop(void) { return 1; }
