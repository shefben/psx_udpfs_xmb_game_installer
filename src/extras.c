#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "extras.h"
#include "util.h"

const char *const opl_art_suffix[OPL_ART_SUFFIXES] = {"COV", "COV2", "ICO", "LAB",
                                                      "LGO", "BG",   "SCR", "SCR2"};

/* ---- OPL cfg ------------------------------------------------------------ */

/* Line [s, e) (without its CR/LF) has `key` before its first '='. */
static int line_is_key(const char *s, const char *e, const char *key) {
  size_t k = strlen(key);
  return (size_t)(e - s) > k && !memcmp(s, key, k) && s[k] == '=';
}

static const char *line_end(const char *s, const char *end) {
  while (s < end && *s != '\n' && *s != '\r')
    s++;
  return s;
}

static const char *next_line(const char *s, const char *end) {
  s = line_end(s, end);
  if (s < end && *s == '\r')
    s++;
  if (s < end && *s == '\n')
    s++;
  return s;
}

int optcfg_get(const char *text, int len, const char *key, char *out, size_t outsz) {
  const char *end = text + len;
  for (const char *s = text; s < end; s = next_line(s, end)) {
    const char *e = line_end(s, end);
    if (line_is_key(s, e, key)) {
      const char *v = s + strlen(key) + 1;
      size_t n = (size_t)(e - v);
      if (outsz) {
        if (n >= outsz)
          n = outsz - 1;
        memcpy(out, v, n);
        out[n] = 0;
      }
      return 1;
    }
  }
  return 0;
}

int optcfg_set(char *text, int len, int cap, const char *key, const char *value) {
  char line[160];
  int ln = (int)strlen(key) + 1 + (int)strlen(value);
  if (ln + 2 >= (int)sizeof(line))
    return -1;
  strcpy(line, key);
  strcat(line, "=");
  strcat(line, value);
  char *end = text + len;
  for (char *s = text; s < end; s = (char *)next_line(s, end)) {
    char *e = (char *)line_end(s, end);
    if (line_is_key(s, e, key)) {
      int old = (int)(e - s);
      if (len - old + ln > cap)
        return -1;
      memmove(s + ln, e, (size_t)(end - e));
      memcpy(s, line, (size_t)ln);
      return len - old + ln;
    }
  }
  /* Append, after a line break if the text does not end with one. */
  int need_nl = len > 0 && text[len - 1] != '\n';
  if (len + need_nl * 2 + ln + 2 > cap)
    return -1;
  if (need_nl) {
    text[len++] = '\r';
    text[len++] = '\n';
  }
  memcpy(text + len, line, (size_t)ln);
  len += ln;
  text[len++] = '\r';
  text[len++] = '\n';
  return len;
}

/* ---- names ----------------------------------------------------------------- */

static const char *ext_of(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot ? dot + 1 : "";
}

static int ext_is(const char *ext, const char *const *list) {
  for (; *list; list++)
    if (!strcasecmp(ext, *list))
      return 1;
  return 0;
}

extra_kind_t extra_classify(const char *name) {
  static const char *const ps2vmc[] = {"bin", "ps2", NULL};
  static const char *const ps1card[] = {"vmc", "mcr", "mcd", "mc", "srm",
                                        "gme", "vmp", "mem", "vgs", NULL};
  const char *e = ext_of(name);
  if (ext_is(e, ps2vmc))
    return EXTRA_PS2_VMC;
  if (!strcasecmp(e, "psu"))
    return EXTRA_PS2_SAVE;
  if (ext_is(e, ps1card))
    return EXTRA_PS1_CARD;
  if (!strcasecmp(e, "mcs"))
    return EXTRA_PS1_SAVE;
  if (!strcasecmp(e, "cht"))
    return EXTRA_PS2_CHEAT;
  if (!strcasecmp(e, "txt"))
    return EXTRA_PS1_CHEAT;
  return EXTRA_NONE;
}

const char *extra_kind_label(extra_kind_t k) {
  switch (k) {
  case EXTRA_PS2_VMC:
    return "PS2 VMC";
  case EXTRA_PS2_SAVE:
    return "PS2 save";
  case EXTRA_PS1_CARD:
    return "PS1 card";
  case EXTRA_PS1_SAVE:
    return "PS1 save";
  case EXTRA_PS2_CHEAT:
    return "PS2 cheats";
  case EXTRA_PS1_CHEAT:
    return "PS1 cheats";
  default:
    return "";
  }
}

int extra_name_id(const char *name, char boot_id[16], int *slot) {
  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  for (int i = 0; i < 4; i++)
    if (!isalpha((unsigned char)base[i]))
      return -1;
  const char *p = base + 4;
  char digits[6];
  int nd = 0;
  if (*p == '_' || *p == '-')
    p++;
  /* SLUS_200.66 | SLUS-20066 | SLUS20066 */
  while (nd < 5 && (isdigit((unsigned char)*p) || (*p == '.' && nd == 3))) {
    if (*p != '.')
      digits[nd++] = *p;
    p++;
  }
  if (nd != 5)
    return -1;
  if (isdigit((unsigned char)*p))
    return -1; /* more digits: not a game ID */
  for (int i = 0; i < 4; i++)
    boot_id[i] = (char)toupper((unsigned char)base[i]);
  boot_id[4] = '_';
  memcpy(boot_id + 5, digits, 3);
  boot_id[8] = '.';
  memcpy(boot_id + 9, digits + 3, 2);
  boot_id[11] = 0;
  *slot = 0;
  /* Slot suffix, then the extension. */
  if (*p == '_' || *p == '-') {
    const char *q = p + 1;
    if (!strncasecmp(q, "SLOT", 4))
      q += 4;
    if ((*q == '0' || *q == '1') && (q[1] == '.' || q[1] == 0))
      *slot = *q - '0';
  }
  return 0;
}

/* ---- PS2 card images --------------------------------------------------------- */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

ps2vmc_kind_t ps2vmc_kind(const uint8_t *head, uint32_t headlen, uint64_t size) {
  static const char magic[] = "Sony PS2 Memory Card Format";
  if (headlen < 0x152 || memcmp(head, magic, sizeof(magic) - 1) != 0)
    return PS2VMC_BAD;
  uint32_t page = rd16(head + 0x28), ppc = rd16(head + 0x2A), clusters = rd32(head + 0x30);
  if (page != 512 || ppc == 0 || clusters == 0 || head[0x150] != 2)
    return PS2VMC_BAD;
  uint64_t raw = (uint64_t)ppc * clusters * page;
  if (raw % (1024 * 1024) != 0)
    return PS2VMC_BAD;
  if (size == raw)
    return PS2VMC_RAW;
  if (size == raw / 512 * 528)
    return PS2VMC_ECC;
  return PS2VMC_BAD;
}

uint64_t ps2vmc_raw_size(ps2vmc_kind_t k, uint64_t size) {
  return k == PS2VMC_ECC ? size / 528 * 512 : k == PS2VMC_RAW ? size : 0;
}

uint32_t ps2vmc_strip_ecc(const uint8_t *in, uint32_t inlen, uint8_t *out) {
  uint32_t o = 0;
  for (uint32_t i = 0; i + 528 <= inlen; i += 528, o += 512)
    memmove(out + o, in + i, 512);
  return o;
}

/* ---- PS1 cards ------------------------------------------------------------------ */

static uint8_t xor_frame(const uint8_t *f) {
  uint8_t x = 0;
  for (int i = 0; i < 127; i++)
    x ^= f[i];
  return x;
}

static void seal(uint8_t *f) { f[127] = xor_frame(f); }

int ps1card_offset(const uint8_t *d, uint32_t len) {
  if (len == PS1_CARD_SIZE && d[0] == 'M' && d[1] == 'C')
    return 0;
  if (len == 0xF40 + PS1_CARD_SIZE && !memcmp(d, "123-456-STD", 11))
    return 0xF40; /* DexDrive .gme */
  if (len == 0x80 + PS1_CARD_SIZE && !memcmp(d, "\0PMV", 4))
    return 0x80; /* PSP .vmp */
  if (len == 64 + PS1_CARD_SIZE && !memcmp(d, "VgsM", 4))
    return 64; /* .mem / .vgs */
  return -1;
}

void ps1card_format(uint8_t *card) {
  memset(card, 0, PS1_CARD_SIZE);
  uint8_t *f = card;
  f[0] = 'M';
  f[1] = 'C';
  seal(f);
  for (int i = 1; i < 16; i++) {
    f = card + PS1_FRAME * i;
    f[0] = 0xA0;
    f[8] = f[9] = 0xFF;
    seal(f);
  }
  for (int i = 16; i < 36; i++) {
    f = card + PS1_FRAME * i;
    memset(f, 0xFF, 4);
    f[8] = f[9] = 0xFF;
    seal(f);
  }
  memset(card + PS1_FRAME * 36, 0xFF, PS1_FRAME * 27); /* 36..62 */
  memcpy(card + PS1_FRAME * 63, card, PS1_FRAME);
}

int ps1card_valid(const uint8_t *card) {
  return card[0] == 'M' && card[1] == 'C' && xor_frame(card) == card[127];
}

static int frame_free(const uint8_t *f) { return (f[0] & 0xF0) == 0xA0; }

int ps1card_free_blocks(const uint8_t *card) {
  int n = 0;
  for (int i = 1; i < 16; i++)
    n += frame_free(card + PS1_FRAME * i);
  return n;
}

/* Delete the save starting at directory frame `first` (1..15). */
static void delete_save(uint8_t *card, int first) {
  int b = first;
  for (int guard = 0; b >= 1 && b <= 15 && guard < 15; guard++) {
    uint8_t *f = card + PS1_FRAME * b;
    int next = (int)rd16(f + 8);
    memset(f, 0, PS1_FRAME);
    f[0] = 0xA0;
    f[8] = f[9] = 0xFF;
    seal(f);
    b = next == 0xFFFF ? -1 : next + 1;
  }
}

int ps1card_insert(uint8_t *card, const uint8_t *mcs, uint32_t len, int replace) {
  if (len < PS1_FRAME + PS1_BLOCK || (len - PS1_FRAME) % PS1_BLOCK || mcs[0] != 0x51)
    return -1;
  int blocks = (int)((len - PS1_FRAME) / PS1_BLOCK);
  if (blocks > 15)
    return -1;
  char name[21];
  memcpy(name, mcs + 0x0A, 20);
  name[20] = 0;
  if (!name[0])
    return -1;
  /* The same file name already on the card. */
  for (int i = 1; i < 16; i++) {
    uint8_t *f = card + PS1_FRAME * i;
    if (f[0] == 0x51 && !strncmp((const char *)f + 0x0A, name, 20)) {
      if (!replace)
        return -3;
      delete_save(card, i);
    }
  }
  int slots[15], n = 0;
  for (int i = 1; i < 16 && n < blocks; i++)
    if (frame_free(card + PS1_FRAME * i))
      slots[n++] = i;
  if (n < blocks)
    return -2;
  for (int k = 0; k < blocks; k++) {
    int b = slots[k];
    uint8_t *f = card + PS1_FRAME * b;
    memset(f, 0, PS1_FRAME);
    f[0] = blocks == 1 ? 0x51 : k == 0 ? 0x51 : k == blocks - 1 ? 0x53 : 0x52;
    if (k == 0) {
      uint32_t size = (uint32_t)blocks * PS1_BLOCK;
      f[4] = (uint8_t)size;
      f[5] = (uint8_t)(size >> 8);
      f[6] = (uint8_t)(size >> 16);
      f[7] = (uint8_t)(size >> 24);
      memcpy(f + 0x0A, name, 20);
    }
    uint32_t next = k == blocks - 1 ? 0xFFFF : (uint32_t)(slots[k + 1] - 1);
    f[8] = (uint8_t)next;
    f[9] = (uint8_t)(next >> 8);
    seal(f);
    memcpy(card + PS1_BLOCK * b, mcs + PS1_FRAME + PS1_BLOCK * k, PS1_BLOCK);
  }
  return 0;
}

/* ---- PS2 saves ------------------------------------------------------------------- */

#define PSU_ENT 512u

static void copy_name(char out[33], const uint8_t *e) {
  memcpy(out, e + 0x40, 32);
  out[32] = 0;
}

int psu_parse(const uint8_t *d, uint32_t len, char dir[33], psu_entry_t *e, int max) {
  if (len < 3 * PSU_ENT)
    return -1;
  uint32_t mode = rd16(d), count = rd32(d + 4);
  char dot[33], dotdot[33];
  copy_name(dir, d);
  copy_name(dot, d + PSU_ENT);
  copy_name(dotdot, d + 2 * PSU_ENT);
  if (!(mode & 0x20) || count < 2 || strcmp(dot, ".") || strcmp(dotdot, "..") || !dir[0] ||
      strchr(dir, '/') || strchr(dir, '\\'))
    return -1;
  int n = (int)count - 2;
  if (n > max)
    return -1;
  uint32_t off = 3 * PSU_ENT;
  for (int i = 0; i < n; i++) {
    if (off + PSU_ENT > len)
      return -1;
    const uint8_t *h = d + off;
    psu_entry_t *x = &e[i];
    x->mode = (uint16_t)rd16(h);
    x->size = rd32(h + 4);
    memcpy(x->ctime, h + 8, 8);
    memcpy(x->mtime, h + 0x18, 8);
    copy_name(x->name, h);
    if (!(x->mode & 0x10) || !x->name[0] || strchr(x->name, '/') || strchr(x->name, '\\') ||
        !strcmp(x->name, ".") || !strcmp(x->name, ".."))
      return -1; /* only plain files */
    off += PSU_ENT;
    x->data_off = off;
    if (x->size > len - off)
      return -1;
    off += (x->size + 1023u) & ~1023u;
    if (off > len)
      off = len; /* the last file's padding may be missing */
  }
  return n;
}
