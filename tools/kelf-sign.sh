#!/usr/bin/env bash
# Sign an EE ELF into a KELF with kelftool, transactionally.
#
#   tools/kelf-sign.sh <input.elf> <output.KELF>
#
# Environment (all explicit; nothing is searched for):
#   PS2KEYS    REQUIRED. Absolute path to your PS2KEYS.dat. The file is
#              never copied: kelftool sees it through a symlink in a
#              private temporary HOME.
#   KELFTOOL   kelftool binary (default: kelftool on PATH).
#   KELF_MODE  dnasload (default) -> kelftool encrypt dnasload <in> <out>
#                   --apptype=0B  (ps2homebrew/kelftool). The header of
#                   the OPL-Launcher boot.kelf and POPSTARTER.KELF that run
#                   from PSX XMB channels: DNASLOAD user header, SystemType
#                   PS2, ApplicationType 11, Flags 0x22C, all MG regions.
#                   Checked byte for byte after signing.
#              mbr  -> kelftool encrypt mbr <in> <out>; note that today's
#                   ps2homebrew/kelftool "mbr" header differs from the one
#                   the old FMCB-compatible fork wrote (that one is
#                   dnasload above).
#              none -> kelftool encrypt <in> <out>; xfwcfw/kelftool, which
#                   always writes a PSX "xosdmain" header (the DESR's own
#                   XMB). A DESR XMB channel with it stays on a black
#                   screen; kept only for comparison.
#              Any other value is an error.
#
# Transaction: the final file is removed first, the KELF is written to
# <output>.tmp, verified, and only then renamed to <output>. On any
# failure the .tmp is removed and the script exits non-zero, so
# <output> exists only after a successful verification.
#
# Verification: non-empty; not an ELF; `kelftool decrypt` (which checks
# the signatures) succeeds and returns the input ELF bytes, followed by
# at most 16 zero bytes of padding (ps2homebrew/kelftool pads).
set -euo pipefail

die() { printf 'kelf-sign: error: %s\n' "$1" >&2; exit 1; }

[ $# -eq 2 ] || die "usage: $0 <input.elf> <output.KELF>"
IN=$1
OUT=$2
TMP=$OUT.tmp
CHECK=$OUT.verify.elf
TMPHOME=

cleanup() {
  rm -f "$TMP" "$CHECK"
  [ -n "$TMPHOME" ] && rm -rf "$TMPHOME"
  return 0
}
trap cleanup EXIT

# Never leave an earlier, now-stale KELF looking current.
rm -f "$OUT" "$TMP" "$CHECK"

[ -s "$IN" ] || die "input ELF not found or empty: $IN"
[ "$(head -c4 "$IN" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || die "input is not an ELF: $IN"

[ -n "${PS2KEYS:-}" ] || die "PS2KEYS is not set.
Run: PS2KEYS=/absolute/path/to/PS2KEYS.dat make kelfs
The key file comes from a console you own; it is never searched for,
copied, committed or packaged by this project."
case "$PS2KEYS" in /*) ;; *) die "PS2KEYS must be an absolute path (got: $PS2KEYS)";; esac
[ -f "$PS2KEYS" ] && [ -s "$PS2KEYS" ] || die "PS2KEYS file missing or empty: $PS2KEYS"

MODE=${KELF_MODE:-dnasload}
OPTS=()
case "$MODE" in
  dnasload) ENC_ARGS=(encrypt dnasload)
            OPTS=(--apptype=0B)
            # A PS2KEYS.dat of bare KEY=VALUE lines (no [section]) is the
            # unnamed INI section to ps2homebrew/kelftool.
            grep -q '^[[:space:]]*\[' "$PS2KEYS" || OPTS+=(--keys=) ;;
  mbr) ENC_ARGS=(encrypt mbr) ;;
  none) ENC_ARGS=(encrypt)
        echo "kelf-sign: WARNING: KELF_MODE=none writes a PSX xosdmain header; DESR XMB channels do not start it" >&2 ;;
  *) die "KELF_MODE must be 'dnasload' (default), 'mbr' or 'none', got '$MODE'" ;;
esac
DEC_OPTS=()
[ "$MODE" = dnasload ] && [[ " ${OPTS[*]} " == *" --keys= "* ]] && DEC_OPTS=(--keys=)

KELFTOOL=${KELFTOOL:-kelftool}
command -v "$KELFTOOL" >/dev/null 2>&1 || die "kelftool not found ($KELFTOOL).
kelftool is a build prerequisite that this project does not ship.
Install it on PATH or set KELFTOOL=/path/to/kelftool."

TMPHOME=$(mktemp -d)
ln -s "$PS2KEYS" "$TMPHOME/PS2KEYS.dat"
mkdir -p "$(dirname "$OUT")"

# kelftool also reads ./PS2KEYS.dat, so it runs inside the private HOME.
ABS_IN=$(readlink -f "$IN")
ABS_TMP=$(cd "$(dirname "$TMP")" && pwd)/$(basename "$TMP")
ABS_CHECK=$(cd "$(dirname "$CHECK")" && pwd)/$(basename "$CHECK")
kt() { (cd "$TMPHOME" && HOME=$TMPHOME "$KELFTOOL" "$@"); }

if ! kt "${ENC_ARGS[@]}" "$ABS_IN" "$ABS_TMP" "${OPTS[@]}" >/dev/null 2>&1; then
  if [ "$MODE" != "none" ]; then
    die "kelftool encrypt $MODE failed.
If your kelftool does not take a header argument (e.g. xfwcfw/kelftool,
whose CLI is 'encrypt <in> <out>' and which always writes a PSX xosdmain
header), use ps2homebrew/kelftool (encrypt <headerid> <in> <out>)."
  fi
  die "kelftool encrypt failed"
fi

[ -s "$TMP" ] || die "kelftool produced no output (missing or unreadable keys?)"
[ "$(head -c4 "$TMP" | od -An -tx1 | tr -d ' \n')" != "7f454c46" ] || die "output is a plain ELF"
[ "$(stat -c %s "$TMP")" -ge 1024 ] || die "output too small for a KELF"
if [ "$MODE" = dnasload ]; then
  # UserDefined (DNASLOAD) and SystemType/ApplicationType/Flags/BitCount/MGZones.
  hdr=$(head -c 32 "$TMP" | od -An -tx1 | tr -d ' \n')
  [ "${hdr:0:32}" = "010000040006004a000e010000000002" ] && [ "${hdr:44:20}" = "000b2c020000ff000000" ] ||
    die "KELF header is not the PSX-compatible dnasload header (got $hdr)"
fi
kt decrypt "$ABS_TMP" "$ABS_CHECK" "${DEC_OPTS[@]}" >/dev/null || die "kelftool decrypt (signature check) failed"
[ -s "$CHECK" ] || die "kelftool decrypt (signature check) failed"
n=$(stat -c %s "$IN")
m=$(stat -c %s "$CHECK")
[ "$m" -ge "$n" ] && [ $((m - n)) -le 16 ] && cmp -s -n "$n" "$IN" "$CHECK" &&
  [ -z "$(tail -c +$((n + 1)) "$CHECK" | tr -d '\0')" ] ||
  die "decrypted KELF content differs from the input ELF"

mv -f "$TMP" "$OUT"
echo "kelf-sign: $OUT ($(stat -c %s "$OUT") bytes, mode $MODE, sha256 $(sha256sum "$OUT" | cut -c1-16)...)"
