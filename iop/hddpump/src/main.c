/*
 * hddpump: writes game data to the HDD on its own IOP thread, so the EE
 * can fetch the next block from the network (fileXio's thread, udpfs)
 * while the previous one is being written (this thread, hdlfs/ATA).
 *
 * The EE copies each block straight into one of the pump's IOP buffers
 * ("slots") by SIF DMA, then submits it; slots are written in submission
 * order. PUMP_SUBMIT returns the next free slot and only blocks while
 * every slot is waiting for the HDD.
 *
 * The same thread also reads data back (PUMP_READ) when no write is
 * queued: into a free slot, then by SIF DMA to the EE with a status
 * record behind it, so the EE checks earlier parts of the copy while the
 * network is busy with the next block.
 *
 * Protocol (rpc id PUMP_RPC_ID, buffers in pump_rpc.h, shared with the EE):
 *   PUMP_BEGIN   open the (already mounted) path, seek, allocate slots
 *   PUMP_SUBMIT  queue slot `slot` (len bytes); return a free slot
 *   PUMP_FLUSH   wait until every queued slot is written
 *   PUMP_END     flush, close, free the slots
 *   PUMP_ATAINFO ATA device 0: atad devinfo, IDENTIFY words 121-124
 *   PUMP_READ    start one read-back (see pump_read_t)
 *   PUMP_SMART   ATA device 0: SMART RETURN STATUS and READ DATA
 *   PUMP_MEMINFO free IOP memory
 */
#include <atad.h>
#include <atahw.h>
#include <errno.h>
#include <intrman.h>
#include <iomanX.h>
#include <irx.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <sifman.h>
#include <stdio.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>
#include <thsemap.h>

#include "pump_rpc.h"

IRX_ID("hddpump", 1, 1);

static SifRpcDataQueue_t rpc_q;
static SifRpcServerData_t rpc_s;
static u32 rpc_buf[64] __attribute__((aligned(64)));
static pump_reply_t reply __attribute__((aligned(64)));
static pump_smart_reply_t smart_reply __attribute__((aligned(64)));

static int fd = -1;
static int nslots;
static u32 slot_size;
static void *slots[PUMP_MAX_SLOTS];
static volatile int slot_busy[PUMP_MAX_SLOTS]; /* EE owns, queued or read-back */
static u32 queue[PUMP_MAX_SLOTS], qlen[PUMP_MAX_SLOTS];
static volatile int qhead, qtail, qcount, writing;
static volatile int err;          /* first write error (<0) */
static volatile u64 written;      /* bytes written since PUMP_BEGIN */
static u32 wsector;               /* next write position (2048-byte sectors) */
static int need_seek;             /* a read moved the file position */
/* Wakes the worker: binary, so a signal is never lost and never piles up. */
static int sema_kick = -1, sema_free = -1, sema_idle = -1;

/* Read-back job (PUMP_READ): pending = accepted, active = running. */
static volatile int rd_pending, rd_active;
static pump_read_t rd;
static pump_read_stat_t rd_stat __attribute__((aligned(64)));

static void kick(void)
{
    SignalSema(sema_kick); /* already signalled: KE_SEMA_OVF, harmless */
}

static int is_idle(void)
{
    return qcount == 0 && !writing && !rd_pending && !rd_active;
}

static void do_write(u32 s, u32 len)
{
    if (!err && fd >= 0) {
        if (need_seek && lseek(fd, (int)wsector, SEEK_SET) != (int)wsector)
            err = -EIO;
        need_seek = 0;
        if (!err) {
            int r = write(fd, slots[s], (int)len);
            if (r != (int)len)
                err = r < 0 ? r : -EIO;
            else {
                written += len;
                wsector += len / 2048;
            }
        }
    }
}

/* Mark the first free slot busy. Interrupts off: the worker and the RPC
 * thread both take slots and either may be preempted mid-scan. Only
 * called after taking sema_free, so one is always free. */
static int claim_slot(void)
{
    int st, s = -1;
    CpuSuspendIntr(&st);
    for (int i = 0; i < nslots; i++)
        if (!slot_busy[i]) {
            slot_busy[i] = 1;
            s = i;
            break;
        }
    CpuResumeIntr(st);
    return s;
}

/* A free slot for the read-back, or -1. Takes it from sema_free like the
 * RPC thread does, so both agree on how many are free. */
static int borrow_slot(void)
{
    if (PollSema(sema_free) != 0)
        return -1;
    int s = claim_slot();
    if (s < 0)
        SignalSema(sema_free);
    return s;
}

static void dma_to_ee(void *src, u32 dest, u32 size, int with_stat)
{
    SifDmaTransfer_t t[2];
    int n = 0;
    if (size) {
        t[n].src = src;
        t[n].dest = (void *)dest;
        t[n].size = (int)((size + 15) & ~15u);
        t[n].attr = 0;
        n++;
    }
    if (with_stat) {
        t[n].src = &rd_stat;
        t[n].dest = (void *)rd.ee_stat;
        t[n].size = sizeof(rd_stat);
        t[n].attr = 0;
        n++;
    }
    if (!n)
        return;
    int st, id;
    do {
        CpuSuspendIntr(&st);
        id = (int)sceSifSetDma(t, n);
        CpuResumeIntr(st);
    } while (id == 0);
    /* Let lower-priority IOP threads (the network) run meanwhile. */
    while (sceSifDmaStat(id) >= 0)
        DelayThread(50);
}

/* Returns 0 when it could not start (no slot yet: retried later). */
static int do_read(void)
{
    int s = borrow_slot();
    if (s < 0)
        return 0;
    int r = -EIO;
    if (fd >= 0 && lseek(fd, (int)rd.sector, SEEK_SET) == (int)rd.sector) {
        need_seek = 1;
        r = read(fd, slots[s], (int)rd.len);
        if (r >= 0 && r != (int)rd.len)
            r = -EIO;
    }
    need_seek = 1;
    rd_stat.seq = rd.seq;
    rd_stat.rc = r;
    dma_to_ee(slots[s], rd.ee_buf, r > 0 ? (u32)r : 0, 0);
    dma_to_ee(NULL, 0, 0, 1); /* status after the data */
    int st;
    CpuSuspendIntr(&st);
    slot_busy[s] = 0;
    CpuResumeIntr(st);
    SignalSema(sema_free);
    return 1;
}

static void worker_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int st, job = 0;
        u32 s = 0, len = 0;
        CpuSuspendIntr(&st);
        if (qcount > 0) { /* writes first: they free the EE's slots */
            s = queue[qhead];
            len = qlen[qhead];
            qhead = (qhead + 1) % PUMP_MAX_SLOTS;
            writing = 1;
            job = 1;
        } else if (rd_pending) {
            rd_pending = 0;
            rd_active = 1;
            job = 2;
        }
        CpuResumeIntr(st);
        if (job == 0) {
            WaitSema(sema_kick);
            continue;
        }
        if (job == 1) {
            do_write(s, len);
            CpuSuspendIntr(&st);
            slot_busy[s] = 0;
            qcount--;
            writing = 0;
            CpuResumeIntr(st);
            SignalSema(sema_free);
        } else if (!do_read()) {
            CpuSuspendIntr(&st);
            rd_active = 0;
            rd_pending = 1; /* no free slot yet: after the next write */
            int stuck = qcount == 0 && !writing;
            CpuResumeIntr(st);
            if (stuck)
                WaitSema(sema_kick);
            continue;
        } else {
            rd_active = 0;
        }
        CpuSuspendIntr(&st);
        int idle = is_idle();
        CpuResumeIntr(st);
        if (idle)
            SignalSema(sema_idle);
    }
}

/* Wait until nothing is queued, being written or read back. */
static void flush(void)
{
    for (;;) {
        int st, idle;
        CpuSuspendIntr(&st);
        idle = is_idle();
        CpuResumeIntr(st);
        if (idle)
            return;
        WaitSema(sema_idle);
    }
}

static int take_free_slot(void)
{
    WaitSema(sema_free);
    return claim_slot();
}

static void release(void)
{
    flush();
    if (fd >= 0)
        close(fd);
    fd = -1;
    for (int i = 0; i < PUMP_MAX_SLOTS; i++) {
        if (slots[i]) {
            int st;
            CpuSuspendIntr(&st);
            FreeSysMemory(slots[i]);
            CpuResumeIntr(st);
        }
        slots[i] = NULL;
    }
    nslots = 0;
}

static void do_begin(const pump_begin_t *b)
{
    release();
    err = 0;
    written = 0;
    qhead = qtail = qcount = 0;
    rd_pending = rd_active = 0;
    need_seek = 0;
    while (PollSema(sema_free) == 0) {
    }
    while (PollSema(sema_idle) == 0) {
    }
    char path[sizeof(b->path)];
    memcpy(path, b->path, sizeof(path));
    path[sizeof(path) - 1] = 0;
    /* Read-write: PUMP_READ reads back through the same descriptor. */
    fd = open(path, O_RDWR);
    if (fd < 0) {
        reply.rc = fd;
        return;
    }
    wsector = b->start_sector;
    if (b->start_sector && lseek(fd, (int)b->start_sector, SEEK_SET) != (int)b->start_sector) {
        reply.rc = -EIO;
        release();
        return;
    }
    slot_size = b->slot_size;
    int want = b->nslots > PUMP_MAX_SLOTS ? PUMP_MAX_SLOTS : (int)b->nslots;
    for (int i = 0; i < want; i++) {
        int st;
        CpuSuspendIntr(&st);
        slots[i] = AllocSysMemory(ALLOC_FIRST, (int)slot_size, NULL);
        CpuResumeIntr(st);
        if (slots[i] == NULL)
            break;
        slot_busy[i] = 0;
        nslots++;
    }
    if (nslots < 2) { /* no overlap possible: the EE uses its own loop */
        reply.rc = -ENOMEM;
        release();
        return;
    }
    for (int i = 0; i < nslots; i++) {
        reply.slot_addr[i] = (u32)slots[i];
        SignalSema(sema_free);
    }
    reply.nslots = (u32)nslots;
    reply.slot_size = slot_size;
    reply.slot = take_free_slot();
    reply.rc = 0;
}

static void do_submit(const pump_submit_t *s)
{
    if (s->slot >= 0 && s->slot < nslots && s->len && s->len <= slot_size && !err) {
        int st;
        CpuSuspendIntr(&st);
        queue[qtail] = (u32)s->slot;
        qlen[qtail] = s->len;
        qtail = (qtail + 1) % PUMP_MAX_SLOTS;
        qcount++;
        CpuResumeIntr(st);
        kick();
    } else if (s->slot >= 0 && s->slot < nslots) {
        slot_busy[s->slot] = 0; /* not written: give it back */
        SignalSema(sema_free);
    }
    reply.slot = s->want_slot ? take_free_slot() : -1;
    reply.rc = err;
}

static void do_read_req(const pump_read_t *r)
{
    if (fd < 0 || !r->len || r->len > slot_size || (r->len & 2047) || (r->ee_buf & 63) ||
        (r->ee_stat & 63)) {
        reply.rc = -EINVAL;
        return;
    }
    int st, busy;
    CpuSuspendIntr(&st);
    busy = rd_pending || rd_active;
    if (!busy) {
        memcpy(&rd, r, sizeof(rd));
        rd_pending = 1;
    }
    CpuResumeIntr(st);
    if (busy) {
        reply.rc = -EBUSY;
        return;
    }
    kick();
    reply.rc = 0;
}

/* ATA device 0 for the EE's 128 GiB warning: atad's view of it and the
 * IDENTIFY words where LBA48-aware DVRP firmware (dvrpwned) puts its
 * "PS2LBA48" signature. atad does not lock its registers against other
 * threads, so this (and PUMP_SMART) is refused while a copy has the
 * partition open (fd >= 0): the worker may be issuing ATA commands. */
static u16 ata_ident[256] __attribute__((aligned(64)));

static void do_atainfo(void)
{
    if (fd >= 0) {
        reply.rc = -EBUSY;
        return;
    }
    ata_devinfo_t *d = sceAtaInit(0);
    if (d != NULL && d->exists) {
        reply.ata_exists = 1;
        reply.ata_lba48 = d->lba48;
        reply.ata_sectors = d->total_sectors;
    }
    if (!reply.ata_exists)
        return;
    memset(ata_ident, 0, sizeof(ata_ident));
    if (sceAtaExecCmd(ata_ident, 1, 0, 0, 0, 0, 0, 0, ATA_C_IDENTIFY_DEVICE) == 0 &&
        sceAtaWaitResult() == 0) {
        reply.ata_identify = 1;
        memcpy(reply.ata_sig, &ata_ident[121], sizeof(reply.ata_sig));
    }
}

/* SMART health of device 0. The DVRP may not pass SMART through: then
 * both results are negative and the EE says "not available". */
static void do_smart(void)
{
    memset(&smart_reply, 0, sizeof(smart_reply));
    if (fd >= 0) { /* a copy is running: see do_atainfo */
        smart_reply.status = smart_reply.data_rc = -EBUSY;
        return;
    }
    ata_devinfo_t *d = sceAtaInit(0);
    if (d == NULL || !d->exists) {
        smart_reply.status = smart_reply.data_rc = -ENODEV;
        return;
    }
    smart_reply.status = sceAtaSmartReturnStatus(0);
    int r = sceAtaExecCmd(smart_reply.data, 1, ATA_S_SMART_READ_DATA, 0, 0, 0x4f, 0xc2, 0,
                          ATA_C_SMART);
    if (r == 0)
        r = sceAtaWaitResult();
    smart_reply.data_rc = r;
}

static void *rpc_handler(int fno, void *buf, int size)
{
    (void)size;
    if (fno == PUMP_SMART) {
        do_smart();
        return &smart_reply;
    }
    memset(&reply, 0, sizeof(reply));
    switch (fno) {
        case PUMP_BEGIN:
            do_begin((const pump_begin_t *)buf);
            break;
        case PUMP_SUBMIT:
            do_submit((const pump_submit_t *)buf);
            break;
        case PUMP_FLUSH:
            flush();
            reply.rc = err;
            break;
        case PUMP_END:
            flush();
            reply.rc = err;
            reply.written = written;
            release();
            break;
        case PUMP_ATAINFO:
            do_atainfo();
            break;
        case PUMP_READ:
            do_read_req((const pump_read_t *)buf);
            break;
        case PUMP_MEMINFO:
            reply.mem_free = QueryTotalFreeMemSize();
            reply.mem_max = QueryMaxFreeMemSize();
            break;
        default:
            reply.rc = -EINVAL;
    }
    reply.written = written;
    return &reply;
}

static void rpc_thread(void *arg)
{
    (void)arg;
    sceSifSetRpcQueue(&rpc_q, GetThreadId());
    sceSifRegisterRpc(&rpc_s, PUMP_RPC_ID, rpc_handler, rpc_buf, NULL, NULL, &rpc_q);
    sceSifRpcLoop(&rpc_q);
}

static int start_thread(void (*fn)(void *), int prio)
{
    iop_thread_t t;
    memset(&t, 0, sizeof(t));
    t.attr = TH_C;
    t.thread = fn;
    t.stacksize = 0x800;
    t.priority = prio;
    int id = CreateThread(&t);
    if (id < 0)
        return id;
    return StartThread(id, NULL);
}

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    iop_sema_t s;
    s.attr = 0;
    s.option = 0;
    s.initial = 0;
    s.max = PUMP_MAX_SLOTS;
    sema_free = CreateSema(&s);
    s.max = 1;
    sema_kick = CreateSema(&s);
    sema_idle = CreateSema(&s);
    if (sema_kick < 0 || sema_free < 0 || sema_idle < 0)
        return MODULE_NO_RESIDENT_END;
    sceSifInitRpc(0);
    /* The worker runs above the RPC thread so a finished write frees its
     * slot before the next submit is looked at. */
    if (start_thread(worker_thread, 0x30) < 0 || start_thread(rpc_thread, 0x38) < 0)
        return MODULE_NO_RESIDENT_END;
    return MODULE_RESIDENT_END;
}
