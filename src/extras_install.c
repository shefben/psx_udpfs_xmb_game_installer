#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <kernel.h>
#include <libmc.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>

#include "app_state.h"
#include "crc32.h"
#include "extras.h"
#include "extras_install.h"
#include "hdd_partitions.h"
#include "iop_boot.h"
#include "manifest.h"
#include "opl_dependency.h"
#include "pops.h"
#include "util.h"

#define W PFS_WORK
#define SMALL_MAX (4u * 1024 * 1024) /* cfg, cheats, art, PS1 cards, .psu */
#define LIST_MAX 256

/* Copy buffer: whole 528-byte pages (ECC strip) and 64-byte aligned. */
static uint8_t g_buf[528 * 512] __attribute__((aligned(64)));
static uint8_t g_back[528 * 512] __attribute__((aligned(64)));

int extras_source_root(char *out, size_t outsz) {
  if (g_app.net == NETWORK_READY) {
    str_copy(out, "udpfs:", outsz);
    return 0;
  }
  int dd = fileXioDopen("mass0:/");
  if (dd >= 0) {
    fileXioDclose(dd);
    str_copy(out, "mass0:", outsz);
    return 0;
  }
  return -1;
}

/* ---- source folders --------------------------------------------------- */

static char g_names[LIST_MAX][64];
static int g_nnames;

/* File names of <root>/<folder> (no subfolders). */
static int list_folder(const char *root, const char *folder) {
  char dir[80];
  snprintf(dir, sizeof(dir), "%s/%s", root, folder);
  g_nnames = 0;
  int dd = fileXioDopen(dir);
  if (dd < 0)
    return dd;
  iox_dirent_t de;
  while (g_nnames < LIST_MAX && fileXioDread(dd, &de) > 0) {
    if (de.name[0] == '.' || (de.stat.mode & FIO_S_IFMT) == FIO_S_IFDIR ||
        strlen(de.name) >= sizeof(g_names[0]))
      continue;
    str_copy(g_names[g_nnames++], de.name, sizeof(g_names[0]));
  }
  fileXioDclose(dd);
  return g_nnames;
}

/* First listed name of kind k for this game and slot (-1: any slot). */
static const char *find_named(extra_kind_t k, const char *boot_id, int slot) {
  for (int i = 0; i < g_nnames; i++) {
    char id[16];
    int s;
    if (extra_classify(g_names[i]) == k && extra_name_id(g_names[i], id, &s) == 0 &&
        !strcmp(id, boot_id) && (slot < 0 || s == slot))
      return g_names[i];
  }
  return NULL;
}

/* ---- verified writes ------------------------------------------------------- */

/* dst holds exactly these bytes already. */
static int same_content(const char *dst, const void *data, uint32_t len) {
  if (file_size(dst) != (int64_t)len)
    return 0;
  void *back = NULL;
  int n = file_load(dst, &back, len + 1);
  int same = n == (int)len && !memcmp(back, data, len);
  free(back);
  return same;
}

/* Put the checked tmp file in dst's place. The old dst is moved aside
 * first and put back if the rename fails, so it is never lost. 0 or <0. */
static int swap_in(const char *tmp, const char *dst) {
  char old[112];
  snprintf(old, sizeof(old), "%.100s.old", dst);
  /* An earlier swap stopped half-way (power cut): if only the .old copy
   * is left it is the user's file, so it goes back first. */
  int had_old = file_size(old) >= 0;
  if (had_old && file_size(dst) < 0 && fileXioRename(old, dst) < 0)
    return -5;
  if (had_old && file_size(old) >= 0)
    fileXioRemove(old);
  int had = file_size(dst) >= 0;
  if (had && fileXioRename(dst, old) < 0)
    return -5;
  int r = fileXioRename(tmp, dst);
  if (r < 0) {
    if (had)
      fileXioRename(old, dst);
    return r;
  }
  if (had)
    fileXioRemove(old);
  return 0;
}

/* Write as dst.tmp, read it back, then replace dst. 0 or <0. */
int extras_write_verified(const char *dst, const void *data, uint32_t len) {
  char tmp[112];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  fileXioRemove(tmp);
  int r = file_write_all(tmp, data, len);
  if (r < 0)
    return r;
  if (!same_content(tmp, data, len)) {
    fileXioRemove(tmp);
    return -5;
  }
  r = swap_in(tmp, dst);
  if (r < 0)
    fileXioRemove(tmp);
  return r;
}

/* Bytes free on the mounted work partition, or 0 if unknown. */
static uint64_t pfs_free_bytes(void) {
  int zs = fileXioDevctl(W, PDIOC_ZONESZ, NULL, 0, NULL, 0);
  int zf = fileXioDevctl(W, PDIOC_ZONEFREE, NULL, 0, NULL, 0);
  return zs > 0 && zf > 0 ? (uint64_t)zs * (uint64_t)zf : 0;
}

/* Stream a (large) card image to dst through dst.tmp, stripping ECC if
 * asked, read back by CRC-32, then replace dst. 0 or <0. */
static int copy_card(const char *src, const char *dst, int strip_ecc, uint64_t raw_size) {
  char tmp[112];
  snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
  int in = fileXioOpen(src, FIO_O_RDONLY);
  if (in < 0)
    return in;
  fileXioRemove(tmp);
  int out = fileXioOpen(tmp, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0644);
  if (out < 0) {
    fileXioClose(in);
    return out;
  }
  uint32_t crc = 0;
  uint64_t done = 0;
  int r = 0;
  while (!r && done < raw_size) {
    int n = fileXioRead(in, g_buf, sizeof(g_buf));
    if (n <= 0 || (strip_ecc && n % 528)) {
      r = n < 0 ? n : -5;
      break;
    }
    uint32_t m = strip_ecc ? ps2vmc_strip_ecc(g_buf, (uint32_t)n, g_buf) : (uint32_t)n;
    if (fileXioWrite(out, g_buf, (int)m) != (int)m)
      r = -5;
    crc = crc32_update(crc, g_buf, m);
    done += m;
  }
  fileXioClose(in);
  fileXioClose(out);
  if (!r && done != raw_size)
    r = -5;
  if (!r) { /* read back */
    int fd = fileXioOpen(tmp, FIO_O_RDONLY);
    uint32_t c2 = 0;
    uint64_t got = 0;
    int n;
    while (fd >= 0 && (n = fileXioRead(fd, g_back, sizeof(g_back))) > 0) {
      c2 = crc32_update(c2, g_back, (uint32_t)n);
      got += (uint64_t)n;
    }
    if (fd >= 0)
      fileXioClose(fd);
    if (fd < 0 || got != raw_size || c2 != crc)
      r = -5;
  }
  if (!r)
    r = swap_in(tmp, dst);
  if (r < 0)
    fileXioRemove(tmp);
  return r < 0 ? r : 0;
}

static void note(extras_report_t *r, const char *fmt, const char *arg) {
  if (!r->note[0])
    snprintf(r->note, sizeof(r->note), fmt, arg);
}

static void what(extras_report_t *r, const char *item) {
  size_t n = strlen(r->what);
  snprintf(r->what + n, sizeof(r->what) - n, "%s%s", n ? ", " : "", item);
}

/* ---- PS2 (OPL) --------------------------------------------------------------- */

/* The OPL prefix on the mounted partition ("pfs1:" or "pfs1:OPL/"). */
static void opl_prefix(const char *partition, char *out, size_t outsz) {
  snprintf(out, outsz, "%s", partition[0] == '+' ? W : W "OPL/");
}

/* Examine a PS2 card image at src: kind and raw size. */
static ps2vmc_kind_t card_kind(const char *src, uint64_t *raw) {
  int64_t size = file_open_size(src);
  int fd = size > 0 ? fileXioOpen(src, FIO_O_RDONLY) : -1;
  if (fd < 0)
    return PS2VMC_BAD;
  static uint8_t head[0x200] __attribute__((aligned(64)));
  int n = fileXioRead(fd, head, sizeof(head));
  fileXioClose(fd);
  ps2vmc_kind_t k = n == (int)sizeof(head) ? ps2vmc_kind(head, (uint32_t)n, (uint64_t)size)
                                           : PS2VMC_BAD;
  *raw = ps2vmc_raw_size(k, (uint64_t)size);
  return k;
}

/* Install card `src` as <prefix>VMC/<ID>_<slot>.bin. 1 written, 0 kept, <0. */
static int put_ps2_card(const char *src, const char *pre, const char *boot_id, int slot,
                        int replace, const char **why) {
  uint64_t raw = 0;
  ps2vmc_kind_t k = card_kind(src, &raw);
  if (k == PS2VMC_BAD) {
    *why = "not a PS2 memory card image (raw 8-64 MiB, or PCSX2 .ps2)";
    return -22;
  }
  char dir[48], dst[96];
  snprintf(dir, sizeof(dir), "%sVMC", pre);
  fileXioMkdir(dir, 0777);
  snprintf(dst, sizeof(dst), "%sVMC/%s_%d.bin", pre, boot_id, slot);
  if (file_size(dst) >= 0 && !replace)
    return 0;
  uint64_t have = pfs_free_bytes(); /* .tmp and the old card coexist until verified. */
  if (have && have < raw + 1024 * 1024) {
    *why = "not enough free space in the OPL partition for the memory card";
    return -28;
  }
  int r = copy_card(src, dst, k == PS2VMC_ECC, raw);
  if (r < 0)
    *why = "copying the memory card failed";
  return r < 0 ? r : 1;
}

/* Point $VMC_<slot> at our card, unless the game uses another card and
 * we may not replace that choice. */
static int cfg_assign_card(char *cfg, int *len, int cap, const char *boot_id, int slot,
                           int replace) {
  char key[8], val[40], cur[40] = "";
  snprintf(key, sizeof(key), "$VMC_%d", slot);
  snprintf(val, sizeof(val), "%s_%d", boot_id, slot);
  if (optcfg_get(cfg, *len, key, cur, sizeof(cur)) && cur[0] && strcmp(cur, val) && !replace)
    return 0;
  int n = optcfg_set(cfg, *len, cap, key, val);
  if (n < 0)
    return -1;
  *len = n;
  return 1;
}

void extras_install_ps2_from(const char *source_root, const char *boot_id, const extras_opts_t *o, extras_report_t *r) {
  memset(r, 0, sizeof(*r));
  char root[16];
  if (source_root) str_copy(root, source_root, sizeof(root));
  else if (extras_source_root(root, sizeof(root)) < 0) return;
  int server = !strcmp(root, "udpfs:");
  const manifest_entry_t *me =
      server && g_manifest_loaded ? manifest_find_id(&g_manifest, boot_id) : NULL;

  /* Gather what exists before touching the HDD. */
  char cfg_src[SOURCE_PATH_MAX] = "", cht_src[160] = "", card_src[2][160] = {"", ""};
  if (me && me->cfg[0])
    snprintf(cfg_src, sizeof(cfg_src), "udpfs:%s", me->cfg);
  else {
    snprintf(cfg_src, sizeof(cfg_src), "%s/CFG/%s.cfg", root, boot_id);
    if (file_open_size(cfg_src) <= 0)
      cfg_src[0] = 0;
  }
  if (list_folder(root, "CHT") > 0) {
    const char *n = find_named(EXTRA_PS2_CHEAT, boot_id, -1);
    if (n)
      snprintf(cht_src, sizeof(cht_src), "%s/CHT/%s", root, n);
  }
  if (list_folder(root, "VMC") > 0)
    for (int s = 0; s < 2; s++) {
      const char *n = find_named(EXTRA_PS2_VMC, boot_id, s);
      if (n)
        snprintf(card_src[s], sizeof(card_src[s]), "%s/VMC/%s", root, n);
    }
  char art_src[OPL_ART_SUFFIXES][96];
  int nart = 0;
  for (int i = 0; i < OPL_ART_SUFFIXES; i++) {
    /* udpfsd converts to the PNG names OPL loads; elsewhere PNG only. */
    if (server && g_manifest_loaded && g_manifest.extras)
      snprintf(art_src[i], sizeof(art_src[i]), "udpfs:/.oplart/%s_%s.png", boot_id,
               opl_art_suffix[i]);
    else
      snprintf(art_src[i], sizeof(art_src[i]), "%s/ART/%s_%s.png", root, boot_id,
               opl_art_suffix[i]);
    if (file_open_size(art_src[i]) > 0)
      nart++;
    else
      art_src[i][0] = 0;
  }
  r->found = !!cfg_src[0] + !!cht_src[0] + !!card_src[0][0] + !!card_src[1][0] + nart;
  if (!r->found)
    return;

  opl_runtime_t opl;
  int rc;
  if (opl_check_runtime(&opl, &rc) != ERR_OK) {
    r->failed = r->found;
    note(r, "%s", "Open PS2 Loader is not installed on the HDD");
    return;
  }
  if (pfs_mount(W, opl.partition, FIO_MT_RDWR) < 0) {
    r->failed = r->found;
    note(r, "cannot mount the OPL partition %s", opl.partition);
    return;
  }
  char pre[24], path[96];
  opl_prefix(opl.partition, pre, sizeof(pre));
  if (pre[strlen(pre) - 1] == '/' && strcmp(pre, W)) {
    snprintf(path, sizeof(path), "%.*s", (int)strlen(pre) - 1, pre);
    fileXioMkdir(path, 0777);
  }

  /* The game's OPL settings: the HDD's, or the server's. */
  static char cfg[16384];
  int clen = 0, cfg_changed = 0;
  char cfg_dst[96];
  snprintf(path, sizeof(path), "%sCFG", pre);
  fileXioMkdir(path, 0777);
  snprintf(cfg_dst, sizeof(cfg_dst), "%sCFG/%s.cfg", pre, boot_id);
  void *data = NULL;
  int had_cfg = file_size(cfg_dst) >= 0;
  int n = had_cfg ? file_load(cfg_dst, &data, sizeof(cfg) - 256) : 0;
  /* The game's settings are there but unreadable (or too big): they are
   * never edited or replaced then, so nothing of them is lost. */
  int cfg_locked = had_cfg && n <= 0;
  if (n > 0)
    memcpy(cfg, data, (size_t)(clen = n));
  free(data);
  data = NULL;
  if (cfg_locked)
    note(r, "%s", "the game's OPL settings could not be read, so they were left as they are");
  if (cfg_src[0]) {
    if (had_cfg && (!o->replace_cfg || cfg_locked)) {
      r->kept++;
    } else if ((n = file_load(cfg_src, &data, sizeof(cfg) - 256)) > 0) {
      memcpy(cfg, data, (size_t)(clen = n));
      cfg_changed = 1;
      what(r, "settings");
    } else {
      r->failed++;
      note(r, "%s", "cannot read the game's settings (CFG)");
    }
    free(data);
    data = NULL;
  }

  /* Cheats, switched on for this game (all codes: the XMB start skips
   * OPL's cheat menu). */
  if (cht_src[0]) {
    int cheat_ready = 0;
    snprintf(path, sizeof(path), "%sCHT", pre);
    fileXioMkdir(path, 0777);
    char dst[96];
    snprintf(dst, sizeof(dst), "%sCHT/%s.cht", pre, boot_id);
    if ((n = file_load(cht_src, &data, SMALL_MAX)) <= 0) {
      r->failed++;
      note(r, "%s", "cannot read the cheat file");
    } else if (same_content(dst, data, (uint32_t)n)) {
      cheat_ready = 1;
      r->kept++;
    } else if (extras_write_verified(dst, data, (uint32_t)n) == 0) {
      cheat_ready = 1;
      r->installed++;
      what(r, "cheats");
    } else {
      r->failed++;
      note(r, "%s", "writing the cheat file failed");
    }
    free(data);
    data = NULL;
    int c1 = cheat_ready ? optcfg_set(cfg, clen, (int)sizeof(cfg), "$CheatsSource", "1") : -1;
    int c2 = c1 > 0 ? optcfg_set(cfg, c1, (int)sizeof(cfg), "$EnableCheat", "1") : -1;
    int c3 = c2 > 0 ? optcfg_set(cfg, c2, (int)sizeof(cfg), "$CheatMode", "0") : -1;
    if (c3 > 0) {
      clen = c3;
      cfg_changed = 1;
    }
  }

  /* Memory cards, assigned to the game's slots. */
  int cards = 0;
  for (int s = 0; s < 2; s++) {
    if (!card_src[s][0])
      continue;
    const char *why = NULL;
    int w = put_ps2_card(card_src[s], pre, boot_id, s, o->replace_cards, &why);
    if (w < 0) {
      r->failed++;
      note(r, "%s", why);
      continue;
    }
    if (w > 0) {
      r->installed++;
      cards++;
    } else {
      r->kept++;
    }
    if (cfg_assign_card(cfg, &clen, (int)sizeof(cfg), boot_id, s, o->replace_cards) > 0)
      cfg_changed = 1;
  }
  if (cards)
    what(r, cards == 2 ? "2 memory cards" : "memory card");

  /* Art, as the PNG names OPL loads. */
  int arts = 0;
  if (nart) {
    snprintf(path, sizeof(path), "%sART", pre);
    fileXioMkdir(path, 0777);
  }
  for (int i = 0; i < OPL_ART_SUFFIXES; i++) {
    if (!art_src[i][0])
      continue;
    char dst[96];
    snprintf(dst, sizeof(dst), "%sART/%s_%s.png", pre, boot_id, opl_art_suffix[i]);
    if ((n = file_load(art_src[i], &data, SMALL_MAX)) <= 0) {
      r->failed++;
    } else if (same_content(dst, data, (uint32_t)n)) {
      r->kept++;
    } else if (extras_write_verified(dst, data, (uint32_t)n) == 0) {
      r->installed++;
      arts++;
    } else {
      r->failed++;
      note(r, "%s", "writing the art failed");
    }
    free(data);
    data = NULL;
  }
  if (arts) {
    char a[16];
    snprintf(a, sizeof(a), "%d art", arts);
    what(r, a);
  }

  if (cfg_changed && !cfg_locked) {
    if (extras_write_verified(cfg_dst, cfg, (uint32_t)clen) == 0)
      r->installed++;
    else {
      r->failed++;
      note(r, "%s", "writing the game's OPL settings failed");
    }
  }
  pfs_umount(W);
}

inst_err_t extras_ps2_card_to_game(const char *src, const char *boot_id, int slot, int replace,
                                   const char **why) {
  *why = NULL;
  opl_runtime_t opl;
  int rc;
  if (opl_check_runtime(&opl, &rc) != ERR_OK) {
    *why = "Open PS2 Loader is not installed on the HDD";
    return ERR_OPL_NOT_FOUND;
  }
  if (pfs_mount(W, opl.partition, FIO_MT_RDWR) < 0) {
    *why = "cannot mount the OPL partition";
    return ERR_PFS_MOUNT;
  }
  char pre[24], cfg_dst[96], dir[48];
  opl_prefix(opl.partition, pre, sizeof(pre));
  if (strcmp(pre, W)) {
    snprintf(dir, sizeof(dir), "%.*s", (int)strlen(pre) - 1, pre);
    fileXioMkdir(dir, 0777);
  }
  inst_err_t e = ERR_OK;
  int w = put_ps2_card(src, pre, boot_id, slot, replace, why);
  if (w < 0) {
    e = ERR_XMB_RESOURCE_WRITE;
  } else if (w == 0) {
    *why = "the game already has a memory card in that slot (choose Replace)";
    e = ERR_PARTITION_EXISTS;
  } else {
    static char cfg[16384];
    int clen = 0;
    snprintf(dir, sizeof(dir), "%sCFG", pre);
    fileXioMkdir(dir, 0777);
    snprintf(cfg_dst, sizeof(cfg_dst), "%sCFG/%s.cfg", pre, boot_id);
    void *data = NULL;
    int had_cfg = file_size(cfg_dst) >= 0;
    int n = had_cfg ? file_load(cfg_dst, &data, sizeof(cfg) - 256) : 0;
    if (n > 0)
      memcpy(cfg, data, (size_t)(clen = n));
    free(data);
    if ((had_cfg && n <= 0) ||
        cfg_assign_card(cfg, &clen, (int)sizeof(cfg), boot_id, slot, 1) < 0 ||
        extras_write_verified(cfg_dst, cfg, (uint32_t)clen) < 0) {
      *why = "the card was copied, but the game's OPL settings could not be updated";
      e = ERR_XMB_RESOURCE_WRITE;
    }
  }
  pfs_umount(W);
  return e;
}

/* ---- PS1 (POPStarter) ------------------------------------------------------- */

static uint8_t g_card[PS1_CARD_SIZE] __attribute__((aligned(64)));

/* A PS1 card file as a raw 128 KiB card in g_card. 0 or -1. */
static int load_ps1_card(const char *src) {
  void *data = NULL;
  int n = file_load(src, &data, PS1_CARD_SIZE + 0x1000);
  int off = n > 0 ? ps1card_offset(data, (uint32_t)n) : -1;
  if (off >= 0)
    memcpy(g_card, (uint8_t *)data + off, PS1_CARD_SIZE);
  free(data);
  return off >= 0 && ps1card_valid(g_card) ? 0 : -1;
}

/* Mount __common and make POPS/<game>/; dst gets that folder. 0 or <0. */
static int pops_folder(const char *partition, char *dir, size_t dirsz) {
  int r = pfs_mount(W, "__common", FIO_MT_RDWR);
  if (r < 0)
    return r;
  char vmc[APA_NAME_MAX + 1];
  pops_vmc_dir(partition, vmc, sizeof(vmc));
  fileXioMkdir(W "POPS", 0777);
  snprintf(dir, dirsz, W "POPS/%s", vmc);
  fileXioMkdir(dir, 0777);
  return 0;
}

/* Insert .mcs `src` into SLOT<slot>.VMC (a new formatted card if none). */
static int put_ps1_save(const char *src, const char *dir, int slot, int replace,
                        const char **why) {
  char dst[96];
  snprintf(dst, sizeof(dst), "%s/SLOT%d.VMC", dir, slot);
  int64_t have = file_size(dst);
  if (have >= 0 && have != PS1_CARD_SIZE) {
    /* Never format over a card that is there: its other saves would go. */
    *why = "the game's memory card is not a 128 KiB PS1 card; not changed";
    return -5;
  }
  if (have == PS1_CARD_SIZE) {
    void *cur = NULL;
    int n = file_load(dst, &cur, PS1_CARD_SIZE + 1);
    if (n == PS1_CARD_SIZE)
      memcpy(g_card, cur, PS1_CARD_SIZE);
    free(cur);
    if (n != PS1_CARD_SIZE || !ps1card_valid(g_card)) {
      *why = "the game's memory card is damaged; not changed";
      return -5;
    }
  } else {
    ps1card_format(g_card);
  }
  void *mcs = NULL;
  int n = file_load(src, &mcs, PS1_FRAME + 15 * PS1_BLOCK + 1);
  int r = n > 0 ? ps1card_insert(g_card, mcs, (uint32_t)n, replace) : -1;
  free(mcs);
  if (r == -1)
    *why = "not a PS1 single save (.mcs)";
  else if (r == -2)
    *why = "not enough free blocks on the game's memory card";
  else if (r == -3)
    return 0; /* already there */
  if (r < 0)
    return -22;
  if (extras_write_verified(dst, g_card, PS1_CARD_SIZE) < 0) {
    *why = "writing the memory card failed";
    return -5;
  }
  return 1;
}

void extras_install_ps1_from(const char *source_root, const char *partition, const char *boot_id, const extras_opts_t *o,
                        extras_report_t *r) {
  memset(r, 0, sizeof(*r));
  char root[16];
  if (source_root) str_copy(root, source_root, sizeof(root));
  else if (extras_source_root(root, sizeof(root)) < 0) return;
  char card_src[2][160] = {"", ""}, save_src[2][160] = {"", ""}, cht_src[160] = "";
  if (list_folder(root, "VMC") > 0)
    for (int s = 0; s < 2; s++) {
      const char *n = find_named(EXTRA_PS1_CARD, boot_id, s);
      if (n)
        snprintf(card_src[s], sizeof(card_src[s]), "%s/VMC/%s", root, n);
      if ((n = find_named(EXTRA_PS1_SAVE, boot_id, s)))
        snprintf(save_src[s], sizeof(save_src[s]), "%s/VMC/%s", root, n);
    }
  if (list_folder(root, "CHT") > 0) {
    const char *n = find_named(EXTRA_PS1_CHEAT, boot_id, -1);
    if (n)
      snprintf(cht_src, sizeof(cht_src), "%s/CHT/%s", root, n);
  }
  r->found = !!card_src[0][0] + !!card_src[1][0] + !!save_src[0][0] + !!save_src[1][0] +
             !!cht_src[0];
  if (!r->found)
    return;
  char dir[64];
  if (pops_folder(partition, dir, sizeof(dir)) < 0) {
    r->failed = r->found;
    note(r, "%s", "cannot mount __common");
    return;
  }
  int cards = 0;
  for (int s = 0; s < 2; s++) {
    char dst[96];
    snprintf(dst, sizeof(dst), "%s/SLOT%d.VMC", dir, s);
    if (card_src[s][0]) {
      if (file_size(dst) >= 0 && !o->replace_cards) {
        r->kept++;
      } else if (load_ps1_card(card_src[s]) < 0) {
        r->failed++;
        note(r, "%s", "a PS1 memory card file is not a 128 KiB card");
      } else if (extras_write_verified(dst, g_card, PS1_CARD_SIZE) == 0) {
        r->installed++;
        cards++;
      } else {
        r->failed++;
        note(r, "%s", "writing a PS1 memory card failed");
      }
    }
    if (save_src[s][0]) {
      const char *why = NULL;
      int w = put_ps1_save(save_src[s], dir, s, o->replace_cards, &why);
      if (w > 0) {
        r->installed++;
        what(r, "save");
      } else if (w == 0) {
        r->kept++;
      } else {
        r->failed++;
        note(r, "%s", why);
      }
    }
  }
  if (cards)
    what(r, cards == 2 ? "2 memory cards" : "memory card");
  if (cht_src[0]) {
    void *data = NULL;
    char dst[96];
    snprintf(dst, sizeof(dst), "%s/CHEATS.TXT", dir);
    int n = file_load(cht_src, &data, SMALL_MAX);
    if (n <= 0) {
      r->failed++;
    } else if (same_content(dst, data, (uint32_t)n)) {
      r->kept++;
    } else if (extras_write_verified(dst, data, (uint32_t)n) == 0) {
      r->installed++;
      what(r, "cheats");
    } else {
      r->failed++;
      note(r, "%s", "writing CHEATS.TXT failed");
    }
    free(data);
  }
  pfs_umount(W);
}

inst_err_t extras_ps1_card_to_game(const char *src, const char *partition, int slot,
                                   int replace, const char **why) {
  *why = NULL;
  if (load_ps1_card(src) < 0) {
    *why = "not a PS1 memory card (raw 128 KiB, .gme, .vmp, .mem)";
    return ERR_INVALID_ARG;
  }
  char dir[64], dst[96];
  if (pops_folder(partition, dir, sizeof(dir)) < 0) {
    *why = "cannot mount __common";
    return ERR_PFS_MOUNT;
  }
  inst_err_t e = ERR_OK;
  snprintf(dst, sizeof(dst), "%s/SLOT%d.VMC", dir, slot);
  if (file_size(dst) >= 0 && !replace) {
    *why = "the game already has a memory card in that slot (choose Replace)";
    e = ERR_PARTITION_EXISTS;
  } else if (extras_write_verified(dst, g_card, PS1_CARD_SIZE) < 0) {
    *why = "writing the memory card failed";
    e = ERR_XMB_RESOURCE_WRITE;
  }
  pfs_umount(W);
  return e;
}

inst_err_t extras_ps1_save_to_game(const char *src, const char *partition, int slot,
                                   int replace, const char **why) {
  *why = NULL;
  char dir[64];
  if (pops_folder(partition, dir, sizeof(dir)) < 0) {
    *why = "cannot mount __common";
    return ERR_PFS_MOUNT;
  }
  int w = put_ps1_save(src, dir, slot, replace, why);
  pfs_umount(W);
  if (w == 0) {
    *why = "a save with that name is already on the card (choose Replace)";
    return ERR_PARTITION_EXISTS;
  }
  return w < 0 ? ERR_XMB_RESOURCE_WRITE : ERR_OK;
}

/* ---- PS2 saves onto a memory card -------------------------------------------- */

static int g_mc_ready = -1, g_mc_reboot = -1;
extern int _iop_reboot_count;

static int mc_ready(void) {
  if (g_mc_ready >= 0 && g_mc_reboot == _iop_reboot_count)
    return g_mc_ready;
  g_mc_reboot = _iop_reboot_count;
  g_mc_ready = iop_load_memcard(&g_app.iop) == 0 && mcInit(MC_TYPE_XMC) >= 0 ? 0 : -1;
  return g_mc_ready;
}

static int mc_wait(void) {
  int cmd = 0, res = 0;
  mcSync(0, &cmd, &res);
  return res;
}

inst_err_t extras_psu_to_card(const char *src, int port, int replace, const char **why) {
  *why = NULL;
  void *data = NULL;
  int n = file_load(src, &data, 16u * 1024 * 1024);
  static psu_entry_t ent[64];
  char dir[33];
  int nf = n > 0 ? psu_parse(data, (uint32_t)n, dir, ent, 64) : -1;
  if (nf < 0) {
    free(data);
    *why = "not a PS2 save (.psu)";
    return ERR_INVALID_ARG;
  }
  if (mc_ready() < 0) {
    free(data);
    *why = "the memory card modules did not load";
    return ERR_INTERNAL;
  }
  int type = 0, freec = 0, fmt = 0;
  mcGetInfo(port, 0, &type, &freec, &fmt);
  int rs = mc_wait();
  if (rs < -1 || type != 2 || !fmt) {
    free(data);
    *why = type == 0 || rs < -2 ? "no PS2 memory card in that slot"
                                : "the memory card is not a formatted PS2 card";
    return ERR_INVALID_ARG;
  }
  int need = 2 + nf;
  for (int i = 0; i < nf; i++)
    need += (int)((ent[i].size + 1023) / 1024);
  /* Room for the whole save first, also when replacing one: mcman
   * deletes a file it re-creates, so running out of space half-way
   * would lose the old save as well as the new one. */
  if (freec < need) {
    free(data);
    *why = "not enough free space on the memory card for the whole save";
    return ERR_NO_SPACE;
  }
  char path[80];
  snprintf(path, sizeof(path), "/%s", dir);
  mcMkDir(port, 0, path);
  int exists = mc_wait() == -4; /* mkdir: -4 = the folder exists */
  if (exists && !replace) {
    free(data);
    *why = "this save is already on the memory card (choose Replace)";
    return ERR_PARTITION_EXISTS;
  }
  inst_err_t e = ERR_OK;
  for (int i = 0; i < nf && !e; i++) {
    snprintf(path, sizeof(path), "/%.32s/%.32s", dir, ent[i].name);
    mcOpen(port, 0, path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
    int fd = mc_wait();
    if (fd < 0) {
      e = ERR_XMB_RESOURCE_WRITE;
      break;
    }
    if (ent[i].size) {
      mcWrite(fd, (uint8_t *)data + ent[i].data_off, (int)ent[i].size);
      if (mc_wait() != (int)ent[i].size)
        e = ERR_XMB_RESOURCE_WRITE;
    }
    mcClose(fd);
    mc_wait();
  }
  free(data);
  if (e)
    *why = "writing to the memory card failed (the save may be incomplete)";
  return e;
}

void extras_install_ps2(const char *id, const extras_opts_t *o, extras_report_t *r) { extras_install_ps2_from(NULL, id, o, r); }
void extras_install_ps1(const char *part, const char *id, const extras_opts_t *o, extras_report_t *r) { extras_install_ps1_from(NULL, part, id, o, r); }
