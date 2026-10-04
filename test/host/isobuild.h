#ifndef PSXI_TEST_ISOBUILD_H
#define PSXI_TEST_ISOBUILD_H

/* Synthetic PS2 disc-image builder for host tests. Produces the first
 * few sectors (PVD, optional UDF VRS, root directory, SYSTEM.CNF) in a
 * small buffer that tests map into a memsrc of any logical size. */

#include <stdint.h>
#include <string.h>

#define ISOB_SECTORS 32
#define ISOB_ROOT_LBA 24
#define ISOB_CNF_LBA 26

typedef struct {
  uint8_t img[ISOB_SECTORS * 2048];
} isob_t;

static inline void isob_put32(uint8_t *p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static inline void isob_put32_both(uint8_t *p, uint32_t v) {
  isob_put32(p, v);
  p[4] = v >> 24; p[5] = v >> 16; p[6] = v >> 8; p[7] = v;
}

/* Append a directory record at *off within sector `sec` of dir. */
static inline void isob_dirrec(uint8_t *sec, uint32_t *off, uint32_t lba,
                               uint32_t size, const char *name, int namelen,
                               int is_dir) {
  uint8_t *r = sec + *off;
  int len = 33 + namelen;
  if (len & 1)
    len++;
  memset(r, 0, len);
  r[0] = len;
  isob_put32_both(r + 2, lba);
  isob_put32_both(r + 10, size);
  r[25] = is_dir ? 2 : 0;
  r[32] = namelen;
  memcpy(r + 33, name, namelen);
  *off += len;
}

/* pvd_blocks: PVD volume space size. syscnf: SYSTEM.CNF text or NULL
 * for "no SYSTEM.CNF". udf: add BEA01/NSR02/TEA01. pad_entries: put
 * this many dummy files before SYSTEM.CNF so it lands in the second
 * root-directory sector. */
static inline void isob_build(isob_t *b, const char *volid, uint32_t pvd_blocks,
                              const char *syscnf, int udf, int pad_entries) {
  memset(b->img, 0, sizeof(b->img));
  uint8_t *pvd = b->img + 16 * 2048;
  pvd[0] = 1;
  memcpy(pvd + 1, "CD001", 5);
  pvd[6] = 1;
  memset(pvd + 40, ' ', 32);
  memcpy(pvd + 40, volid, strlen(volid));
  isob_put32_both(pvd + 80, pvd_blocks);
  pvd[128] = 0x00; pvd[129] = 0x08; /* block size 2048 LE */
  pvd[130] = 0x08; pvd[131] = 0x00;
  uint32_t root_size = pad_entries ? 2 * 2048 : 2048;
  uint32_t o = 156 - 156;
  uint8_t rootrec[34];
  memset(rootrec, 0, sizeof(rootrec));
  isob_dirrec(rootrec, &o, ISOB_ROOT_LBA, root_size, "\0", 1, 1);
  memcpy(pvd + 156, rootrec, 34);

  uint8_t *term = b->img + 17 * 2048;
  term[0] = 255;
  memcpy(term + 1, "CD001", 5);
  term[6] = 1;
  if (udf) {
    memcpy(b->img + 18 * 2048 + 1, "BEA01", 5);
    memcpy(b->img + 19 * 2048 + 1, "NSR02", 5);
    memcpy(b->img + 20 * 2048 + 1, "TEA01", 5);
  }

  uint8_t *root = b->img + ISOB_ROOT_LBA * 2048;
  uint32_t off = 0;
  isob_dirrec(root, &off, ISOB_ROOT_LBA, root_size, "\0", 1, 1);
  isob_dirrec(root, &off, ISOB_ROOT_LBA, root_size, "\1", 1, 1);
  uint8_t *cur = root;
  for (int i = 0; i < pad_entries; i++) {
    char nm[32];
    int n = 0;
    nm[n++] = 'F';
    nm[n++] = 'A' + (i / 26) % 26;
    nm[n++] = 'A' + i % 26;
    memcpy(nm + n, "_PADDING_NAME_XXXXXXXX.BIN;1", 28);
    n += 28;
    if (cur == root && off + 33 + n + 1 > 2048) {
      cur = root + 2048;
      off = 0;
    }
    isob_dirrec(cur, &off, 0, 0, nm, n, 0);
  }
  if (syscnf) {
    if (pad_entries && cur == root) { /* force into sector 2 */
      cur = root + 2048;
      off = 0;
    }
    isob_dirrec(cur, &off, ISOB_CNF_LBA, (uint32_t)strlen(syscnf),
                "SYSTEM.CNF;1", 12, 0);
    memcpy(b->img + ISOB_CNF_LBA * 2048, syscnf, strlen(syscnf));
  }
}

#endif
