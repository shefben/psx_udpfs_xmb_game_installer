#ifndef PSXI_XMB_TEXT_H
#define PSXI_XMB_TEXT_H

#include <stddef.h>

/* Generated text resources for XMB channels (plan sections 17, 19).
 * Pure functions; byte output is covered by host tests. */

/* The OSD-header system.cnf for every PFS-booted channel (game and
 * installer). LF line endings, as hdl-dump writes them. */
#define XMB_SYSTEM_CNF                                                         \
  "BOOT2 = pfs:/EXECUTE.KELF\n"                                                \
  "VER = 1.00\n"                                                               \
  "VMODE = NTSC\n"                                                             \
  "HDDUNITPOWER = NICHDD\n"

/* Copy `in` to `out`, dropping control characters (< 0x20, 0x7F) so a
 * value can never inject extra lines into info.sys. Leading/trailing
 * spaces are trimmed. Bytes >= 0x80 (UTF-8) are kept. */
void xmb_sanitize_value(const char *in, char *out, size_t outsz);

/* Render res/info.sys from the plan template with CRLF line endings.
 * `title` and `title_id` are sanitized. Returns the byte length, or 0
 * if `outsz` is too small. */
size_t xmb_render_info_sys(char *out, size_t outsz, const char *title,
                           const char *title_id);

/* info.sys for a game: title_id = "<PART ID> (<REGION>)", e.g.
 * "SLUS-20312 (NTSC-U)". Returns length or 0. */
size_t xmb_game_info_sys(char *out, size_t outsz, const char *title,
                         const char *boot_id);

#endif
