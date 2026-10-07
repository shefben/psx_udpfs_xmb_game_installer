#ifndef PUMP_RPC_H
#define PUMP_RPC_H

/* hddpump RPC: shared by the IOP module and the EE client (src/pump.c).
 * Plain fixed-width types so both compilers lay the structs out alike. */

#define PUMP_RPC_ID 0x50534850 /* "PSHP" */
#define PUMP_MAX_SLOTS 8

enum { PUMP_BEGIN = 1, PUMP_SUBMIT, PUMP_FLUSH, PUMP_END, PUMP_ATAINFO,
       PUMP_READ, PUMP_SMART, PUMP_MEMINFO };

typedef struct {
  char path[48];          /* e.g. "hdl0:" (mounted by the EE) */
  unsigned int start_sector; /* hdlfs lseek unit: 2048-byte sectors */
  unsigned int slot_size;    /* bytes, multiple of 2048 */
  unsigned int nslots;       /* wanted (2..PUMP_MAX_SLOTS) */
  unsigned int pad;
} pump_begin_t;

typedef struct {
  int slot;          /* filled slot to queue, or -1 */
  unsigned int len;  /* bytes in it */
  int want_slot;     /* 1: return a free slot (blocks while all are busy) */
  unsigned int pad;
} pump_submit_t;

typedef struct {
  int rc;            /* 0, or the first write error (<0) */
  int slot;          /* free slot for the EE, -1 if none asked */
  unsigned int nslots;
  unsigned int slot_size;
  unsigned long long written; /* bytes written since PUMP_BEGIN */
  unsigned int slot_addr[PUMP_MAX_SLOTS]; /* IOP addresses (PUMP_BEGIN) */
  /* PUMP_ATAINFO: ATA device 0 (on the DESR: the DVRP's PS2 area) */
  unsigned int ata_exists;     /* atad probed it */
  unsigned int ata_lba48;      /* it reports the LBA48 command set */
  unsigned int ata_sectors;    /* atad's total_sectors (the PS2 area) */
  unsigned int ata_identify;   /* 1: IDENTIFY DEVICE read, ata_sig valid */
  unsigned short ata_sig[4];   /* IDENTIFY words 121-124 */
  /* PUMP_MEMINFO: IOP memory (sysmem) */
  unsigned int mem_free;       /* total free bytes */
  unsigned int mem_max;        /* largest free block */
} pump_reply_t;

/* PUMP_READ (read-back while the copy runs): the pump's worker reads
 * `len` bytes at 2048-byte sector `sector` of the open partition into a
 * free slot, after the writes queued before it, then SIF-DMAs them to
 * the EE at `ee_buf` followed by a pump_read_stat_t at `ee_stat` (both
 * 64-byte aligned EE addresses). The RPC itself returns at once: rc 0
 * (started), -EBUSY (one is still running) or -EINVAL. One at a time. */
typedef struct {
  unsigned int sector;
  unsigned int len;    /* multiple of 2048, <= slot_size */
  unsigned int ee_buf;
  unsigned int ee_stat;
  unsigned int seq;    /* echoed in pump_read_stat_t.seq */
  unsigned int pad[3];
} pump_read_t;

typedef struct {
  unsigned int seq; /* request's seq once the data is in place */
  int rc;           /* bytes read, or <0 */
  unsigned int pad[2];
} pump_read_stat_t;

/* PUMP_SMART (no pump_begin needed): ATA device 0. */
typedef struct {
  int status;     /* SMART RETURN STATUS: 0 ok, 1 threshold exceeded, <0 failed */
  int data_rc;    /* SMART READ DATA: 0 when data[] is valid */
  unsigned int pad[2];
  unsigned char data[512];
} pump_smart_reply_t;

#endif
