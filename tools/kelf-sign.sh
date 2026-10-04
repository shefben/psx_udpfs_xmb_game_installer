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
#   KELF_MODE  mbr  (default, canonical) -> kelftool encrypt mbr <in> <out>
#                   the form OPL-Launcher documents.
#              none (experimental fallback, only if a DESR rejects the
#                   canonical KELF) -> kelftool encrypt <in> <out>
#              Any other value is an error.
#
# Transaction: the final file is removed first, the KELF is written to
# <output>.tmp, verified, and only then renamed to <output>. On any
# failure the .tmp is removed and the script exits non-zero, so
# <output> exists only after a successful verification.
#
# Verification: non-empty; not an ELF; `kelftool decrypt` (which checks
# the signatures) succeeds and returns exactly the input ELF bytes.
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

MODE=${KELF_MODE:-mbr}
case "$MODE" in
  mbr) ENC_ARGS=(encrypt mbr) ;;
  none) ENC_ARGS=(encrypt)
        echo "kelf-sign: WARNING: KELF_MODE=none is an experimental fallback" >&2 ;;
  *) die "KELF_MODE must be 'mbr' (default) or 'none', got '$MODE'" ;;
esac

KELFTOOL=${KELFTOOL:-kelftool}
command -v "$KELFTOOL" >/dev/null 2>&1 || die "kelftool not found ($KELFTOOL).
kelftool is a build prerequisite that this project does not ship.
Install it on PATH or set KELFTOOL=/path/to/kelftool."

TMPHOME=$(mktemp -d)
ln -s "$PS2KEYS" "$TMPHOME/PS2KEYS.dat"
mkdir -p "$(dirname "$OUT")"

if ! HOME=$TMPHOME "$KELFTOOL" "${ENC_ARGS[@]}" "$IN" "$TMP" >/dev/null 2>&1; then
  if [ "$MODE" = "mbr" ]; then
    die "kelftool encrypt mbr failed.
If your kelftool does not take a mode argument (e.g. xfwcfw/kelftool,
whose CLI is 'encrypt <in> <out>' and which always writes a PSX/DESR
header), the canonical mbr mode is not available with it; build with
KELF_MODE=none explicitly, or use a fork that supports 'encrypt mbr'."
  fi
  die "kelftool encrypt failed"
fi

[ -s "$TMP" ] || die "kelftool produced no output"
[ "$(head -c4 "$TMP" | od -An -tx1 | tr -d ' \n')" != "7f454c46" ] || die "output is a plain ELF"
[ "$(stat -c %s "$TMP")" -ge 1024 ] || die "output too small for a KELF"
HOME=$TMPHOME "$KELFTOOL" decrypt "$TMP" "$CHECK" >/dev/null || die "kelftool decrypt (signature check) failed"
cmp -s "$IN" "$CHECK" || die "decrypted KELF content differs from the input ELF"

mv -f "$TMP" "$OUT"
echo "kelf-sign: $OUT ($(stat -c %s "$OUT") bytes, mode $MODE, sha256 $(sha256sum "$OUT" | cut -c1-16)...)"
