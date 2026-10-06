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

/* info.sys values used where nothing is known (never left empty). */
#define XMB_UNKNOWN "Unknown"
#define XMB_NOTE "Installed with UDPFS Game Installer"
#define XMB_WEB "https://github.com/shefben/psx_udpfs_xmb_game_installer"

/* Render res/info.sys from the plan template with CRLF line endings.
 * `title` and `title_id` are sanitized. Returns the byte length, or 0
 * if `outsz` is too small. */
size_t xmb_render_info_sys(char *out, size_t outsz, const char *title,
                           const char *title_id, const char *today);

/* XMB game info from udpfsd's /.udpfsd/info/<ID>.txt (key=value lines:
 * release_date YYYYMMDD, developer, publisher, genre). */
typedef struct {
  char release_date[9];
  char developer[64];
  char publisher[64];
  char genre[32];
} xmb_game_info_t;

/* Parse an info text; unknown keys are ignored, a release date that is
 * not 8 digits is dropped. Returns the number of fields set. */
int xmb_game_info_parse(const char *text, xmb_game_info_t *gi);

/* xmb_game_info_sys with the info fields filled in (gi may be NULL).
 * `today` ("YYYYMMDD", may be NULL) is the release date when none is
 * known, so the XMB never gets an empty one. */
size_t xmb_game_info_sys_ex(char *out, size_t outsz, const char *title, const char *boot_id,
                            const xmb_game_info_t *gi, const char *today);

/* Value of "<key> = value" in an info.sys text (exact key). 0 or -1. */
int xmb_info_sys_get(const char *text, const char *key, char *out, size_t outsz);

/* The same info.sys with only its title line replaced (sanitized; an
 * empty title is refused). Returns the new length, or 0. */
size_t xmb_info_sys_retitle(const char *text, const char *title, char *out, size_t outsz);

/* info.sys for a game: title_id = the partition ID ("SLUS-20312") and
 * area = the region letter, as PFS-BatchKit-Manager and PSX-XMB-Manager
 * write them. Returns length or 0. */
size_t xmb_game_info_sys(char *out, size_t outsz, const char *title,
                         const char *boot_id);

/* info.sys "area" letter from the ID's third character (BatchKit):
 * U (SLUS/SCUS), E (SLES/SCES), J (SLPS/SLPM/SCPS), A, C, K; else X. */
char xmb_area_letter(const char *boot_id);

/* OSD-header system.cnf of a PS2 game (one visible HDL partition, as
 * PFS-BatchKit-Manager installs it): the XMB starts the boot KELF stored
 * in the header (OPL-Launcher). Byte-identical to BatchKit's (hdl_dump's
 * HDL_HDR1). */
#define XMB_PATINFO_SYSTEM_CNF                                                 \
  "BOOT2 = PATINFO\n"                                                          \
  "VER = 1.20\n"                                                               \
  "VMODE = NTSC\n"                                                             \
  "HDDUNITPOWER = NICHDD\n"
/* HDD-format icon.sys ("PS2X") for the OSD header: title0 = title (the
 * XMB name), title1 = second line (game ID), in PFS-BatchKit-Manager's
 * exact form (hdl_dump colours and lights, bgcola 0, empty uninstall
 * messages). LF line endings. Returns length or 0. */
size_t xmb_render_icon_sys(char *out, size_t outsz, const char *title0, const char *title1);

/* Default res/man.xml (the XMB "Manual" entry), as PFS-BatchKit-Manager's
 * template: background image/0.png, pages image/1.png and image/2.png
 * (written blank when the channel has no manual). The title is
 * XML-escaped. Returns length or 0. */
size_t xmb_render_man_xml(char *out, size_t outsz, const char *title);

/* "YYYYMMDD" for a valid date (year 2000-2099), else -1. */
int xmb_date_str(int year, int month, int day, char out[9]);

#endif
