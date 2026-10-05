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
 * Protocol (rpc id PUMP_RPC_ID, buffers in pump_rpc.h, shared with the EE):
 *   PUMP_BEGIN  open the (already mounted) path, seek, allocate slots
 *   PUMP_SUBMIT queue slot `slot` (len bytes); return a free slot
 *   PUMP_FLUSH  wait until every queued slot is written
 *   PUMP_END    flush, close, free the slots
 */
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

IRX_ID("hddpump", 1, 0);

static SifRpcDataQueue_t rpc_q;
static SifRpcServerData_t rpc_s;
static u32 rpc_buf[64] __attribute__((aligned(64)));
static pump_reply_t reply __attribute__((aligned(64)));

static int fd = -1;
static int nslots;
static u32 slot_size;
static void *slots[PUMP_MAX_SLOTS];
static volatile int slot_busy[PUMP_MAX_SLOTS]; /* EE owns or queued */
static u32 queue[PUMP_MAX_SLOTS], qlen[PUMP_MAX_SLOTS];
static volatile int qhead, qtail, qcount, writing;
static volatile int err;          /* first write error (<0) */
static volatile u64 written;      /* bytes written since PUMP_BEGIN */
static int sema_work = -1, sema_free = -1, sema_idle = -1;

static void writer_thread(void *arg)
{
    (void)arg;
    for (;;) {
        WaitSema(sema_work);
        int st;
        CpuSuspendIntr(&st);
        u32 s = queue[qhead], len = qlen[qhead];
        qhead = (qhead + 1) % PUMP_MAX_SLOTS;
        writing = 1;
        CpuResumeIntr(st);
        if (!err && fd >= 0) {
            int r = write(fd, slots[s], (int)len);
            if (r != (int)len)
                err = r < 0 ? r : -EIO;
            else
                written += len;
        }
        CpuSuspendIntr(&st);
        slot_busy[s] = 0;
        qcount--;
        writing = 0;
        int idle = qcount == 0;
        CpuResumeIntr(st);
        SignalSema(sema_free);
        if (idle)
            SignalSema(sema_idle);
    }
}

/* Wait until nothing is queued or being written. */
static void flush(void)
{
    for (;;) {
        int st, idle;
        CpuSuspendIntr(&st);
        idle = qcount == 0 && !writing;
        CpuResumeIntr(st);
        if (idle)
            return;
        WaitSema(sema_idle);
    }
}

static int take_free_slot(void)
{
    WaitSema(sema_free);
    for (int i = 0; i < nslots; i++)
        if (!slot_busy[i]) {
            slot_busy[i] = 1;
            return i;
        }
    return -1;
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
    while (PollSema(sema_free) == 0) {
    }
    while (PollSema(sema_idle) == 0) {
    }
    char path[sizeof(b->path)];
    memcpy(path, b->path, sizeof(path));
    path[sizeof(path) - 1] = 0;
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        reply.rc = fd;
        return;
    }
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
        SignalSema(sema_work);
    } else if (s->slot >= 0 && s->slot < nslots) {
        slot_busy[s->slot] = 0; /* not written: give it back */
        SignalSema(sema_free);
    }
    reply.slot = s->want_slot ? take_free_slot() : -1;
    reply.rc = err;
}

static void *rpc_handler(int fno, void *buf, int size)
{
    (void)size;
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
    sema_work = CreateSema(&s);
    sema_free = CreateSema(&s);
    s.max = 1;
    sema_idle = CreateSema(&s);
    if (sema_work < 0 || sema_free < 0 || sema_idle < 0)
        return MODULE_NO_RESIDENT_END;
    sceSifInitRpc(0);
    /* The writer runs above the RPC thread so a finished write frees its
     * slot before the next submit is looked at. */
    if (start_thread(writer_thread, 0x30) < 0 || start_thread(rpc_thread, 0x38) < 0)
        return MODULE_NO_RESIDENT_END;
    return MODULE_RESIDENT_END;
}
