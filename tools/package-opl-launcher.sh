#!/usr/bin/env bash
# Sign the pinned, unmodified OPL-Launcher build as the per-game
# EXECUTE.KELF (plan section 22). Also stages it at
# vendor/opl-launcher/EXECUTE.KELF so the next installer build embeds it.
#
#   tools/package-opl-launcher.sh [OPL-Launcher.elf] [out.KELF]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IN=${1:-$ROOT/build/opl-launcher/OPL-Launcher.elf}
OUT=${2:-$ROOT/dist/opl-launcher-EXECUTE.KELF}
[ -s "$IN" ] || { echo "error: $IN missing - run 'make opl-launcher'" >&2; exit 1; }
bash "$ROOT/tools/kelf-sign.sh" "$IN" "$OUT"
mkdir -p "$ROOT/vendor/opl-launcher"
cp "$OUT" "$ROOT/vendor/opl-launcher/EXECUTE.KELF"
echo "staged vendor/opl-launcher/EXECUTE.KELF (re-run 'make installer' to embed it)"
