#ifndef PSXI_EXTRAS_H
#define PSXI_EXTRAS_H

#include <stddef.h>
#include <stdint.h>

/* Game extras (pure): memory cards, saves, cheats, OPL settings and art,
 * and where each belongs on the HDD. The console side (extras_install.c)
 * copies them; everything here is host-tested. */

/* ---- OPL (Open PS2 Loader 3e3f34e) -----------------------------------
 * Folders at the OPL prefix (partition root for "+OPL", else "OPL/"):
 *   CFG/<STARTUP>.cfg   per-game settings, "key=value" lines, CRLF
 *   CHT/<STARTUP>.cht   cheats; on with $CheatsSource=1 $EnableCheat=1
 *                       $CheatMode=0 (0 = apply all) in the game's cfg
 *   VMC/<name>.bin      raw PS2 card images (no ECC), $VMC_0 / $VMC_1 =
 *                       <name> (without .bin, <= 31 chars) in the cfg
 *   ART/<STARTUP>_<SUFFIX>.png  PNG only */

#define OPL_ART_SUFFIXES 8
extern const char *const opl_art_suffix[OPL_ART_SUFFIXES]; /* COV ... SCR2 */

/* Set key=value in an OPL cfg text: replaces the first line with that
 * key, else appends one. Lines written end in CRLF. text holds len bytes
 * (cap total). Returns the new length, or -1 if it would not fit. */
int optcfg_set(char *text, int len, int cap, const char *key, const char *value);

/* Value of `key` into out. 1 found, 0 not. */
int optcfg_get(const char *text, int len, const char *key, char *out, size_t outsz);

/* ---- Files in the server's VMC / CHT folders -------------------------- */

typedef enum {
  EXTRA_NONE = 0,
  EXTRA_PS2_VMC,  /* .bin (OPL), .ps2 (PCSX2, with ECC) card image */
  EXTRA_PS2_SAVE, /* .psu (uLaunchELF / PS2 Save Builder export) */
  EXTRA_PS1_CARD, /* .vmc .mcr .mcd .mc .srm .gme .vmp .mem .vgs */
  EXTRA_PS1_SAVE, /* .mcs single save */
  EXTRA_PS2_CHEAT, /* .cht (OPL) */
  EXTRA_PS1_CHEAT, /* .txt (POPStarter CHEATS.TXT) */
} extra_kind_t;

extra_kind_t extra_classify(const char *name);
const char *extra_kind_label(extra_kind_t k);

/* Game ID and slot from a file name: "SLUS_200.66_1.bin", "SLUS-20066.VMC",
 * "SCES_123.45.cht", "SLUS20066_SLOT1.mcr". boot_id gets the startup
 * form "SLUS_200.66"; slot is 0 or 1 (from _0/_1/_SLOT0/_SLOT1, default
 * 0). 0, or -1 if the name does not start with a game ID. */
int extra_name_id(const char *name, char boot_id[16], int *slot);

/* ---- PS2 card images ------------------------------------------------- */

typedef enum { PS2VMC_BAD = 0, PS2VMC_RAW, PS2VMC_ECC } ps2vmc_kind_t;

/* From the superblock (first 0x154 bytes) and the file size: RAW is what
 * OPL accepts (size = pages_per_cluster * clusters * 512, a multiple of
 * 1 MiB, card type 2); ECC is the same card with 16 ECC bytes after every
 * 512-byte page (PCSX2 .ps2), which ps2vmc_strip_ecc turns into RAW. */
ps2vmc_kind_t ps2vmc_kind(const uint8_t *head, uint32_t headlen, uint64_t size);

/* Size of the raw card for an image of `size` bytes and kind k. */
uint64_t ps2vmc_raw_size(ps2vmc_kind_t k, uint64_t size);

/* ECC image bytes (whole 528-byte pages) to raw pages; returns bytes out. */
uint32_t ps2vmc_strip_ecc(const uint8_t *in, uint32_t inlen, uint8_t *out);

/* ---- PS1 memory cards (POPStarter SLOT0.VMC / SLOT1.VMC) --------------- */

#define PS1_CARD_SIZE 131072u
#define PS1_BLOCK 8192u
#define PS1_FRAME 128u

/* Where the raw 128 KiB card starts in a card file (raw, DexDrive .gme,
 * PSP .vmp, .mem/.vgs), or -1 if it is none of them. */
int ps1card_offset(const uint8_t *data, uint32_t len);

/* A freshly formatted card. */
void ps1card_format(uint8_t *card);

/* Header frame "MC" with a valid checksum. */
int ps1card_valid(const uint8_t *card);

/* Free blocks (of 15). */
int ps1card_free_blocks(const uint8_t *card);

/* Copy a single save (.mcs: its 128-byte first directory frame, then its
 * blocks) onto the card: links and checksums rebuilt. A save with the
 * same file name is replaced if `replace`, else -3. Returns 0, -1 bad
 * .mcs, -2 not enough free blocks, -3 exists. */
int ps1card_insert(uint8_t *card, const uint8_t *mcs, uint32_t len, int replace);

/* ---- PS2 saves (.psu) -------------------------------------------------- */

typedef struct {
  char name[33];
  uint16_t mode;
  uint32_t size;
  uint32_t data_off; /* offset of its data in the .psu */
  uint8_t ctime[8], mtime[8];
} psu_entry_t;

/* The save folder name and its files (no subfolders). Returns the number
 * of files, or -1 if malformed or more than `max`. */
int psu_parse(const uint8_t *d, uint32_t len, char dir[33], psu_entry_t *e, int max);

#endif
