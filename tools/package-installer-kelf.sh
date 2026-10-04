#!/usr/bin/env bash
# Sign the installer ELF as the EXECUTE.KELF for PP.UDPFS-INSTALLER
# (plan sections 21-22). It is staged beside the bootstrap ELF; the
# installer reads it from udpfs:/PAYLOAD/installer-EXECUTE.KELF during
# "Install/Repair Installer XMB App" (see docs/INSTALL.md).
#
#   tools/package-installer-kelf.sh [installer.elf] [out.KELF]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IN=${1:-$ROOT/dist/desr-udpfs-installer.elf}
OUT=${2:-$ROOT/dist/installer-EXECUTE.KELF}
[ -s "$IN" ] || { echo "error: $IN missing - run 'make installer'" >&2; exit 1; }
bash "$ROOT/tools/kelf-sign.sh" "$IN" "$OUT"
