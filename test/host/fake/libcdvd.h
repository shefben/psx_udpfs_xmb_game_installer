#ifndef FAKE_LIBCDVD_H
#define FAKE_LIBCDVD_H
/* Host stand-in for libcdvd (source_cdvd.c tests, fake_cdvd.c). */
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct {
  u8 trycount, spindlctrl, datapattern, pad;
} sceCdRMode;
enum { SCECdSecS2048 = 0 };
enum { SCECdSpinNom = 1 };
enum { SCECdErNO = 0 };
enum { SCECdNODISC = 0 };
enum { SCECdComplete = 2, SCECdNotReady = 6 };
enum { SCECdTrayOpen = 0 };
enum { SCECdINoD = 1 };
int sceCdInit(int mode);
int sceCdGetDiskType(void);
int sceCdDiskReady(int mode);
int sceCdRead(u32 lsn, u32 sectors, void *buf, sceCdRMode *mode);
int sceCdSync(int mode);
int sceCdGetError(void);
int sceCdTrayReq(int param, u32 *traychk);
int sceCdStop(void);
#endif
