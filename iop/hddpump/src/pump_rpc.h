#ifndef PUMP_RPC_H
#define PUMP_RPC_H

/* hddpump RPC: shared by the IOP module and the EE client (src/pump.c).
 * Plain fixed-width types so both compilers lay the structs out alike. */

#define PUMP_RPC_ID 0x50534850 /* "PSHP" */
#define PUMP_MAX_SLOTS 8

enum { PUMP_BEGIN = 1, PUMP_SUBMIT, PUMP_FLUSH, PUMP_END };

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
  unsigned int pad[2];
} pump_reply_t;

#endif
