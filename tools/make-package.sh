#!/usr/bin/env bash
# Build the end-user zip from dist/ (run `make dist` first):
#
#   <name>/README.txt
#   <name>/PS2/desr-udpfs-installer-bootstrap.elf
#   <name>/PC/udpfsd/  server binaries, udpfsd.cfg, OPL-Launcher KELF,
#                      OPNPS2LD.ELF + licence, empty DVD CD CFG ART
#
# Usage: make-package.sh <dist dir> <output zip>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIST=$1 OUT=$2
NAME=$(basename "$OUT" .zip)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

(cd "$DIST" && sha256sum -c SHA256SUMS --quiet)

P=$STAGE/$NAME
mkdir -p "$P/PS2" "$P/PC/udpfsd"
cp "$ROOT/docs/package/README.txt" "$P/"
cp "$ROOT/CHANGELOG.md" "$P/CHANGELOG.txt"
cp "$ROOT/docs/SERVER_MANUAL.md" "$P/SERVER-MANUAL.txt"
cp "$DIST/desr-udpfs-installer-bootstrap.elf" "$P/PS2/"
cp "$DIST/desr-udpfs-installer-app.elf" "$DIST/installer-EXECUTE.KELF" \
   "$DIST/opl-launcher-PSX1.KELF" "$P/PS2/"
cp "$DIST/BUILD-MANIFEST.txt" "$P/"
[ ! -f "$DIST/RELEASE-NOTES.txt" ] || cp "$DIST/RELEASE-NOTES.txt" "$P/"
cp "$DIST/udpfsd/udpfsd-windows-amd64.exe" "$DIST/udpfsd/udpfsd-linux-amd64" \
   "$DIST/udpfsd/opl-launcher-EXECUTE.KELF" "$DIST/udpfsd/OPNPS2LD.ELF" \
   "$DIST/udpfsd/OPL-LICENSE.txt" "$P/PC/udpfsd/"
cp "$ROOT/docs/package/udpfsd.cfg" "$P/PC/udpfsd/"
chmod +x "$P/PC/udpfsd/udpfsd-linux-amd64"
# The server refuses a configured folder that does not exist, and some
# unzip tools drop empty folders: give each one a note.
# POPStarter (pinned rev13 Beta KELF, unchanged).
mkdir -p "$P/PC/udpfsd/POPS"
[ -f "$DIST/udpfsd/POPS/POPSTARTER.KELF" ] && cp "$DIST/udpfsd/POPS/POPSTARTER.KELF" "$P/PC/udpfsd/POPS/"
for d in DVD CD CFG ART APPS VMC CHT; do
  mkdir -p "$P/PC/udpfsd/$d"
done
echo "Put DVD game images (.iso / .zso / .cso / .chd, or split .iso.001 sets) in this folder." > "$P/PC/udpfsd/DVD/_put_DVD_games_here.txt"
echo "Put CD game images (.iso / .zso / .cso / .chd, or split .iso.001 sets) in this folder." > "$P/PC/udpfsd/CD/_put_CD_games_here.txt"
echo "Optional: OPL per-game settings named <GAME-ID>.cfg, e.g. SLUS_203.12.cfg." > "$P/PC/udpfsd/CFG/_optional_OPL_settings_here.txt"
echo "Optional: covers named <GAME-ID>_COV.png or .jpg, e.g. SLUS_203.12_COV.jpg." > "$P/PC/udpfsd/ART/_optional_covers_here.txt"
printf '%s\n' "Homebrew apps (.ELF) to install as XMB channels (Apps menu on the DESR)." "" \
  "One app per folder, e.g. APPS\wLaunchELF\BOOT.ELF with the files it needs;" \
  "the installer offers to copy the whole folder. A lone .ELF works too." > "$P/PC/udpfsd/APPS/_put_apps_here.txt"
printf '%s\n' "PS1 games: put .VCD files or single-BIN .cue sets here. BIN/CUE conversion is automatic." "" \
  "POPSTARTER.KELF (POPStarter rev13 Beta) is already here." "" \
  "Also needed here, NOT included (Sony's POPS emulator, supply your own):" \
  "  POPS.ELF" \
  "  IOPRP252.IMG" > "$P/PC/udpfsd/POPS/_put_PS1_VCD_games_here.txt"
echo "Memory cards / saves: <GAME-ID>_0.bin / _1.bin (PS2), .ps2, PS1 .mcr / .gme / .vmp / .mcs, PS2 .psu." > "$P/PC/udpfsd/VMC/_put_cards_and_saves_here.txt"
echo "Cheats: <GAME-ID>.cht for OPL, <GAME-ID>.txt for POPStarter." > "$P/PC/udpfsd/CHT/_put_cheats_here.txt"
# Windows line endings for the files people open in Notepad.
for f in "$P/README.txt" "$P/CHANGELOG.txt" "$P/SERVER-MANUAL.txt" "$P/PC/udpfsd/udpfsd.cfg" "$P"/PC/udpfsd/*/_*.txt; do
  sed -i 's/\r$//; s/$/\r/' "$f"
done

rm -f "$OUT"
# Python's zipfile (no `zip` dependency); folders get their own entries
# and file modes are kept (udpfsd-linux-amd64 stays executable).
python3 - "$STAGE" "$NAME" "$OUT" <<'EOF'
import os, sys, zipfile
stage, name, out = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(os.path.join(stage, name)):
        dirs.sort()
        rel = os.path.relpath(root, stage)
        z.write(root, rel + "/")
        for f in sorted(files):
            z.write(os.path.join(root, f), os.path.join(rel, f))
EOF
echo "package: $OUT"
(cd "$STAGE" && find "$NAME" -type f | sort)
