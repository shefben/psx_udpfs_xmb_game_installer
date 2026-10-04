#!/usr/bin/env bash
# Sign an EE ELF into a KELF with kelftool. Used by the package-*.sh
# scripts. Contains no key material.
#
#   tools/kelf-sign.sh <input.elf> <output.KELF>
#
# Environment:
#   KELFTOOL   kelftool binary (default: kelftool on PATH)
#   PS2KEYS    path to your PS2KEYS.dat (default: ./keys/PS2KEYS.dat,
#              then ~/PS2KEYS.dat)
#   KELF_MODE  header argument for forks that take one. Default "mbr",
#              the form OPL-Launcher documents:
#                 kelftool encrypt mbr <in> <out>
#              Set KELF_MODE=none for forks whose CLI is
#                 kelftool encrypt <in> <out>
#
# The script refuses to produce output unless kelftool succeeds and the
# result is a non-empty file that is not a plain ELF, so an unsigned
# ELF can never end up under a .KELF name.
set -euo pipefail

die() { printf 'kelf-sign: error: %s\n' "$1" >&2; exit 1; }

[ $# -eq 2 ] || die "usage: $0 <input.elf> <output.KELF>"
IN=$1
OUT=$2
[ -s "$IN" ] || die "input ELF not found or empty: $IN"
[ "$(head -c4 "$IN" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || die "input is not an ELF: $IN"

KELFTOOL=${KELFTOOL:-kelftool}
command -v "$KELFTOOL" >/dev/null 2>&1 || die "kelftool not found.
kelftool is a build prerequisite that this project does not ship.
Build or install a kelftool fork and put it on PATH, or set KELFTOOL=..."

ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ -n "${PS2KEYS:-}" ]; then KEYS=$PS2KEYS
elif [ -f "$ROOT/keys/PS2KEYS.dat" ]; then KEYS=$ROOT/keys/PS2KEYS.dat
elif [ -f "$HOME/PS2KEYS.dat" ]; then KEYS=$HOME/PS2KEYS.dat
else die "PS2KEYS.dat not found (set PS2KEYS=/path/to/PS2KEYS.dat).
Key material comes from a console you own and is never committed."
fi
[ -s "$KEYS" ] || die "key file empty or unreadable: $KEYS"

# kelftool reads ~/PS2KEYS.dat; give it a private HOME holding a link to
# the chosen key file so nothing is copied.
TMPHOME=$(mktemp -d)
trap 'rm -rf "$TMPHOME"' EXIT
ln -s "$(cd "$(dirname "$KEYS")" && pwd)/$(basename "$KEYS")" "$TMPHOME/PS2KEYS.dat"

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
MODE=${KELF_MODE:-mbr}
if [ "$MODE" = "none" ]; then
  HOME=$TMPHOME "$KELFTOOL" encrypt "$IN" "$OUT"
else
  HOME=$TMPHOME "$KELFTOOL" encrypt "$MODE" "$IN" "$OUT"
fi

[ -s "$OUT" ] || die "kelftool produced no output"
if [ "$(head -c4 "$OUT" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; then
  rm -f "$OUT"
  die "output is a plain ELF, refusing to keep it as a KELF"
fi
python3 "$ROOT/tools/verify-assets.py" --kelf "$OUT"
echo "kelf-sign: $OUT ($(wc -c < "$OUT") bytes)"
