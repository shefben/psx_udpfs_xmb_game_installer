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
  pump_read_t r;
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

/* Read-back status, written by the IOP's DMA: read it uncached. A whole
 * cache line of its own, so no write to a neighbour can ever write a
 * stale copy back over it. */
static union {
  pump_read_stat_t s;
  uint8_t line[64];
} rd_line __attribute__((aligned(64)));
#define rd_stat (rd_line.s)
static uint32_t rd_seq;
static int rd_running;

int pump_read_start(uint32_t sector, void *dst, uint32_t len) {
  if (rd_running)
    return -16; /* -EBUSY */
  if (((uintptr_t)dst & 63) || !len || (len & 2047))
    return -22;
  rd_seq++;
  if (!rd_seq)
    rd_seq = 1;
  /* Nothing of dst or the status may be left in the cache to be written
   * back over the DMA'd data later. */
  memset(&rd_line, 0, sizeof(rd_line));
  SifWriteBackDCache(&rd_line, sizeof(rd_line));
  SifWriteBackDCache(dst, (int)len);
  memset(&req, 0, sizeof(req));
  pump_read_t *r = &req.r;
  r->sector = sector;
  r->len = len;
  r->ee_buf = (uint32_t)(uintptr_t)dst;
  r->ee_stat = (uint32_t)(uintptr_t)&rd_stat;
  r->seq = rd_seq;
  int rc = call(PUMP_READ, sizeof(*r));
  if (rc == 0)
    rd_running = 1;
  return rc;
}

int pump_read_poll(int *got) {
  if (!rd_running)
    return -22;
  volatile pump_read_stat_t *s = (volatile pump_read_stat_t *)UNCACHED_SEG(&rd_stat);
  if (s->seq != rd_seq)
    return 0;
  *got = s->rc;
  rd_running = 0;
  return 1;
}

int pump_read_wait(int *got) {
  int r;
  while ((r = pump_read_poll(got)) == 0)
    ;
  return r;
}

int pump_smart(pump_smart_t *out) {
  static pump_smart_reply_t sr __attribute__((aligned(64)));
  memset(out, 0, sizeof(*out));
  if (pump_init() < 0)
    return -19;
  memset(&req, 0, sizeof(req));
  if (SifCallRpc(&cd, PUMP_SMART, 0, &req, 16, &sr, sizeof(sr), NULL, NULL) < 0)
    return -5;
  out->status = sr.status;
  out->data_rc = sr.data_rc;
  memcpy(out->data, sr.data, sizeof(out->data));
  return 0;
}

int pump_meminfo(uint32_t *free_bytes, uint32_t *max_block) {
  if (pump_init() < 0)
    return -19;
  memset(&req, 0, sizeof(req));
  int r = call(PUMP_MEMINFO, 16);
  if (r < 0)
    return r;
  *free_bytes = rep.mem_free;
  *max_block = rep.mem_max;
  return 0;
}

int pump_flush(void) {
  memset(&req, 0, sizeof(req));
  return call(PUMP_FLUSH, 16);
}

int pump_end(uint64_t *written) {
  int got;
  if (rd_running) /* the IOP still DMAs into the EE buffer: let it finish */
    pump_read_wait(&got);
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
