#ifndef PSXI_PARTNAME_H
#define PSXI_PARTNAME_H

/* Canonical partition naming (plan section 11).
 *
 * Mirrors hdl-dump's hdl_pname() byte-for-byte for valid inputs:
 *
 *   index  0..2   prefix "PP." (visible) or "__." (hidden)
 *   index  3..6   region letters, uppercased; non-letters -> 'X'
 *   index  7      '-'
 *   index  8..12  five digits; non-digits -> '0'
 *   index 13..14  ".."
 *   index 15..    title, truncated to 16 bytes, uppercased,
 *                 anything outside [A-Z0-9] -> '_'
 *
 * The visible/hidden pair is the OPL-Launcher boot ABI: the launcher
 * rewrites bytes 0..1 of its own PP. partition name to "__" to find
 * the game, so both names come from one function and can never drift.
 */

#define APA_NAME_MAX 32
#define PART_TITLE_MAX 16 /* PS2_PART_IDMAX - 1 - 3 - 10 - 2 */

#define BOOT_ID_LEN 11 /* "SLUS_203.12" */
#define PART_ID_LEN 10 /* "SLUS-20312"  */

/* Extract the BOOT form (e.g. "SLUS_203.12") from a SYSTEM.CNF BOOT2
 * value such as "cdrom0:\SLUS_203.12;1". Strips the device prefix,
 * any directory components and the ";1" version suffix. Returns 0
 * and writes `out` (>= 16 bytes) on success, -1 if the result is not
 * of the form XXXX_NNN.NN. */
int boot_id_from_boot2(const char *boot2_value, char out[16]);

/* 1 if `id` is exactly XXXX_NNN.NN (letters, underscore, digits). */
int boot_id_is_valid(const char *id);

/* "SLUS_203.12" -> "SLUS-20312". Returns 0, or -1 for an invalid id. */
int boot_id_to_part_id(const char *boot_id, char out[PART_ID_LEN + 1]);

/* Build the paired names. `startup_id` must be a valid BOOT form id.
 * An empty title is allowed (hdl-dump allows it) but callers should
 * prefer a non-empty display title. Returns 0 on success, -1 on
 * invalid input. */
int build_game_partition_pair(const char *startup_id, const char *display_title,
                              char visible[APA_NAME_MAX + 1],
                              char hidden[APA_NAME_MAX + 1]);

/* 1 if both names are well-formed game pair members whose bytes from
 * index 2 onward are identical and whose prefixes are "PP"/"__". */
int partition_pair_matches(const char *visible, const char *hidden);

/* 1 if `name` looks like a visible ("PP.XXXX-NNNNN..") game channel,
 * 0 otherwise. The installer partition PP.UDPFS-INSTALLER is NOT a
 * game channel. */
int partition_is_game_channel(const char *name);

/* 1 if `name` looks like a hidden ("__.XXXX-NNNNN..") game partition. */
int partition_is_hidden_game(const char *name);

/* Swap the two-character prefix: "PP.x" <-> "__.x". Returns 0, or -1 if
 * `name` has neither prefix. */
int partition_partner(const char *name, char out[APA_NAME_MAX + 1]);

/* Region label for info.sys title_id (plan section 19). Never NULL. */
const char *region_label(const char *startup_id);

#endif
