#include <string.h>

#include "partname.h"
#include "util.h"

static int is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static int is_lower(char c) { return c >= 'a' && c <= 'z'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static char to_upper(char c) { return is_lower(c) ? c - 'a' + 'A' : c; }

int boot_id_is_valid(const char *id) {
  if (!id || strlen(id) != BOOT_ID_LEN)
    return 0;
  for (int i = 0; i < 4; i++)
    if (!is_upper(id[i]))
      return 0;
  if (id[4] != '_' || id[8] != '.')
    return 0;
  static const int digits[] = {5, 6, 7, 9, 10};
  for (unsigned i = 0; i < sizeof(digits) / sizeof(digits[0]); i++)
    if (!is_digit(id[digits[i]]))
      return 0;
  return 1;
}

int boot_id_from_boot2(const char *value, char out[16]) {
  out[0] = 0;
  if (!value)
    return -1;
  /* Skip the device ("cdrom0:") and any directories. */
  const char *p = strchr(value, ':');
  p = p ? p + 1 : value;
  for (const char *q = p; *q; q++)
    if (*q == '\\' || *q == '/')
      p = q + 1;

  char id[16];
  int n = 0;
  while (*p && *p != ';' && *p != ' ' && *p != '\t' && *p != '\r' &&
         *p != '\n' && n < (int)sizeof(id) - 1)
    id[n++] = to_upper(*p++);
  id[n] = 0;
  if (!boot_id_is_valid(id))
    return -1;
  memcpy(out, id, (size_t)n + 1);
  return 0;
}

int boot_id_to_part_id(const char *boot_id, char out[PART_ID_LEN + 1]) {
  out[0] = 0;
  if (!boot_id_is_valid(boot_id))
    return -1;
  /* Same byte shuffle as hdl_pname: SLUS_203.12 -> SLUS-20312. */
  memcpy(out, boot_id, 8);
  out[4] = '-';
  out[8] = boot_id[9];
  out[9] = boot_id[10];
  out[10] = 0;
  return 0;
}

static int build_one(const char prefix[3], const char *startup_id,
                     const char *title, char out[APA_NAME_MAX + 1]) {
  char part_id[PART_ID_LEN + 1];
  if (boot_id_to_part_id(startup_id, part_id) < 0)
    return -1;

  memcpy(out, prefix, 3);
  memcpy(out + 3, part_id, PART_ID_LEN);
  out[13] = '.';
  out[14] = '.';
  size_t tlen = strlen(title);
  if (tlen > PART_TITLE_MAX)
    tlen = PART_TITLE_MAX;
  for (size_t i = 0; i < tlen; i++) {
    char c = to_upper(title[i]);
    out[15 + i] = (is_upper(c) || is_digit(c)) ? c : '_';
  }
  out[15 + tlen] = 0;
  return 0;
}

int build_game_partition_pair(const char *startup_id, const char *display_title,
                              char visible[APA_NAME_MAX + 1],
                              char hidden[APA_NAME_MAX + 1]) {
  visible[0] = hidden[0] = 0;
  if (!startup_id || !display_title)
    return -1;
  if (build_one("PP.", startup_id, display_title, visible) < 0)
    return -1;
  memcpy(hidden, visible, APA_NAME_MAX + 1);
  hidden[0] = '_';
  hidden[1] = '_';
  return 0;
}

/* Shape check shared by both prefixes: "xx.AAAA-NNNNN.." then title. */
static int has_game_shape(const char *name) {
  size_t n = strlen(name);
  if (n < 15 || n > APA_NAME_MAX || name[2] != '.' || name[7] != '-' ||
      name[13] != '.' || name[14] != '.')
    return 0;
  for (int i = 3; i < 7; i++)
    if (!is_upper(name[i]))
      return 0;
  for (int i = 8; i < 13; i++)
    if (!is_digit(name[i]))
      return 0;
  /* Title bytes are exactly what build_game_partition_pair (and
   * hdl-dump) produce; anything else, notably ',', is foreign. */
  for (size_t i = 15; i < n; i++)
    if (!is_upper(name[i]) && !is_digit(name[i]) && name[i] != '_')
      return 0;
  return 1;
}

int partition_is_game_channel(const char *name) {
  return name && name[0] == 'P' && name[1] == 'P' && has_game_shape(name);
}

int partition_is_hidden_game(const char *name) {
  return name && name[0] == '_' && name[1] == '_' && has_game_shape(name);
}

int partition_pair_matches(const char *visible, const char *hidden) {
  return partition_is_game_channel(visible) &&
         partition_is_hidden_game(hidden) && strcmp(visible + 2, hidden + 2) == 0;
}

int partition_partner(const char *name, char out[APA_NAME_MAX + 1]) {
  out[0] = 0;
  if (!name || strlen(name) > APA_NAME_MAX || strlen(name) < 3 || name[2] != '.')
    return -1;
  const char *np;
  if (name[0] == 'P' && name[1] == 'P')
    np = "__";
  else if (name[0] == '_' && name[1] == '_')
    np = "PP";
  else
    return -1;
  str_copy(out, name, APA_NAME_MAX + 1);
  out[0] = np[0];
  out[1] = np[1];
  return 0;
}

int part_id_from_partition(const char *name, char out[16]) {
  out[0] = 0;
  if (!partition_is_game_channel(name) && !partition_is_hidden_game(name))
    return -1;
  /* "xx.SLUS-20312.." -> "SLUS_203.12" */
  memcpy(out, name + 3, 4);
  out[4] = '_';
  memcpy(out + 5, name + 8, 3);
  out[8] = '.';
  memcpy(out + 9, name + 11, 2);
  out[11] = 0;
  return boot_id_is_valid(out) ? 0 : -1;
}

int partition_remove_allowed(const char *name, unsigned apa_type) {
  if (!name || !name[0] || strlen(name) > APA_NAME_MAX || strchr(name, ','))
    return 0;
  if (partition_is_hidden_game(name))
    return apa_type == APA_TYPE_HDL_ID;
  if (partition_is_game_channel(name))
    return apa_type == APA_TYPE_PFS_ID;
  if (!strcmp(name, INSTALLER_PARTITION_NAME) || !strcmp(name, TEST_PARTITION_NAME))
    return apa_type == APA_TYPE_PFS_ID;
  return 0;
}

int hdd_dirent_is_main(unsigned mode, unsigned attr) {
  return mode != 0 && !(attr & 0x0001 /* APA_FLAG_SUB */);
}

const char *region_label(const char *id) {
  static const struct {
    const char *prefix;
    const char *label;
  } MAP[] = {
      {"SLUS", "NTSC-U"}, {"SCUS", "NTSC-U"}, {"SLES", "PAL"},
      {"SCES", "PAL"},    {"SLPS", "NTSC-J"}, {"SLPM", "NTSC-J"},
      {"SCPS", "NTSC-J"}, {"SCAJ", "NTSC-J"}, {"SCKA", "NTSC-J"},
  };
  if (!id || strlen(id) < 4)
    return "UNKNOWN";
  for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
    if (strncmp(id, MAP[i].prefix, 4) == 0)
      return MAP[i].label;
  return "UNKNOWN";
}
