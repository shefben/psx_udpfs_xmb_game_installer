#ifndef FAKE_FILEXIO_RPC_H
#define FAKE_FILEXIO_RPC_H

/* Host stand-in for PS2SDK's fileXio client, enough for the network
 * sources (source_udpfs.c, source_wire.c). The file behind every handle
 * is fake_fxio_* (test/host/fake_filexio.c): a plain image whose byte at
 * offset o is fake_byte(o), or udpfsd's LZ4-frame view of it. NOWAIT
 * reads complete after a few polls; a read issued while one is still
 * pending, or any other call meanwhile, is recorded as a violation. */

#include <stdint.h>

#define FXIO_WAIT 0
#define FXIO_NOWAIT 1
#define FXIO_COMPLETE 1
#define FXIO_INCOMPLETE 0

typedef struct {
  unsigned int mode;
  unsigned int attr;
  unsigned int size;
  unsigned char ctime[8], atime[8], mtime[8];
  unsigned int hisize;
} iox_stat_t;

typedef struct {
  iox_stat_t stat;
  char name[256];
  void *unknown;
} iox_dirent_t;

int fileXioOpen(const char *path, int flags, ...);
int fileXioClose(int fd);
int fileXioRead(int fd, void *buf, int size);
int64_t fileXioLseek64(int fd, int64_t off, int whence);
int fileXioSetBlockMode(int mode);
int fileXioWaitAsync(int mode, int *ret);

/* ---- test control ---- */
uint8_t fake_byte(uint64_t off);
void fake_fxio_reset(uint64_t size, int wire_view);
extern int fake_fxio_violations; /* calls made while a NOWAIT read was pending */
extern int fake_fxio_reads;      /* fileXioRead calls */
extern int fake_fxio_fail_read;  /* >0: that fileXioRead returns -5 */
extern int fake_fxio_short_read; /* >0: that read returns half the bytes */
extern uint32_t fake_fxio_max_frame; /* wire view: raw bytes per frame cap (0: none) */
extern int fake_fxio_wire_lz4;       /* wire view: LZ4 frames instead of stored */

#endif
