#include <kernel.h>
#include <sifrpc.h>
#include <string.h>

#include "../iop/hddpump/src/pump_rpc.h"
#include "pump.h"

static SifRpcClientData_t cd __attribute__((aligned(64)));
static int bound_reboot = -1;
static union {
  pump_begin_t b;
  pump_submit_t s;
  uint8_t pad[64];
} req __attribute__((aligned(64)));
static pump_reply_t rep __attribute__((aligned(64)));
static uint32_t slot_addr[PUMP_MAX_SLOTS];
static int cur_slot = -1;

extern int _iop_reboot_count;

static int call(int fno, uint32_t size) {
  if (SifCallRpc(&cd, fno, 0, &req, size, &rep, sizeof(rep), NULL, NULL) < 0)
    return -5;
  return rep.rc;
}

int pump_init(void) {
  if (bound_reboot == _iop_reboot_count && cd.server)
    return 0;
  memset(&cd, 0, sizeof(cd));
  for (int i = 0; i < 50; i++) {
    if (SifBindRpc(&cd, PUMP_RPC_ID, 0) < 0)
      return -5;
    if (cd.server) {
      bound_reboot = _iop_reboot_count;
      return 0;
    }
    nopdelay();
  }
  return -19; /* module not running */
}

int pump_atainfo(pump_atainfo_t *out) {
  memset(out, 0, sizeof(*out));
  if (pump_init() < 0)
    return -19;
  memset(&req, 0, sizeof(req));
  int r = call(PUMP_ATAINFO, 16);
  if (r < 0)
    return r;
  out->exists = (int)rep.ata_exists;
  out->lba48 = (int)rep.ata_lba48;
  out->sectors = rep.ata_sectors;
  out->identify = (int)rep.ata_identify;
  memcpy(out->sig, rep.ata_sig, sizeof(out->sig));
  return 0;
}

int pump_begin(const char *path, uint32_t start_sector, uint32_t slot_size, int nslots,
               uint32_t *slot_size_out) {
  if (pump_init() < 0)
    return -19;
  memset(&req, 0, sizeof(req));
  strncpy(req.b.path, path, sizeof(req.b.path) - 1);
  req.b.start_sector = start_sector;
  req.b.slot_size = slot_size;
  req.b.nslots = (unsigned)nslots;
  int r = call(PUMP_BEGIN, sizeof(req.b));
  if (r < 0)
    return r;
  for (unsigned i = 0; i < rep.nslots && i < PUMP_MAX_SLOTS; i++)
    slot_addr[i] = rep.slot_addr[i];
  cur_slot = rep.slot;
  *slot_size_out = rep.slot_size;
  return cur_slot >= 0 ? 0 : -5;
}

int pump_put(const void *buf, uint32_t len) {
  if (cur_slot < 0)
    return -5;
  /* EE -> IOP buffer by SIF DMA (source written back from the cache). */
  SifWriteBackDCache((void *)buf, (int)len);
  SifDmaTransfer_t t;
  t.src = (void *)buf;
  t.dest = (void *)slot_addr[cur_slot];
  t.size = (int)len;
  t.attr = 0;
  unsigned id;
  while ((id = SifSetDma(&t, 1)) == 0)
    nopdelay();
  while (SifDmaStat(id) >= 0)
    ;
  memset(&req, 0, sizeof(req));
  req.s.slot = cur_slot;
  req.s.len = len;
  req.s.want_slot = 1;
  int r = call(PUMP_SUBMIT, sizeof(req.s));
  cur_slot = rep.slot;
  return r < 0 ? r : (cur_slot >= 0 ? 0 : -5);
}

int pump_flush(void) {
  memset(&req, 0, sizeof(req));
  return call(PUMP_FLUSH, 16);
}

int pump_end(uint64_t *written) {
  memset(&req, 0, sizeof(req));
  /* Hand the unused current slot back without writing it. */
  if (cur_slot >= 0) {
    req.s.slot = cur_slot;
    req.s.len = 0;
    req.s.want_slot = 0;
    call(PUMP_SUBMIT, sizeof(req.s));
    cur_slot = -1;
  }
  memset(&req, 0, sizeof(req));
  int r = call(PUMP_END, 16);
  if (written)
    *written = rep.written;
  return r;
}
