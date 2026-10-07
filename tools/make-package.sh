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
echo "Put DVD game images (.iso .zso .cso .chd, or split .iso.001 .002 ...) in this folder." > "$P/PC/udpfsd/DVD/_put_DVD_games_here.txt"
echo "Put CD game images (.iso .zso .cso .chd) here; PS1 games (.VCD or BIN/CUE) too." > "$P/PC/udpfsd/CD/_put_CD_games_here.txt"
echo "Optional: OPL per-game settings named <GAME-ID>.cfg, e.g. SLUS_203.12.cfg." > "$P/PC/udpfsd/CFG/_optional_OPL_settings_here.txt"
printf '%s\n' "Optional art named <GAME-ID>_<TYPE>.png or .jpg, e.g. SLUS_203.12_COV.jpg." "" \
  "COV is the XMB cover. All of these are also installed for OPL (as PNG):" \
  "  COV COV2 ICO LAB LGO BG (or BG_00) SCR (or SCR_00) SCR2 (or SCR_01)" > "$P/PC/udpfsd/ART/_optional_covers_here.txt"
printf '%s\n' "Memory cards and saves, installed with the game or from" \
  "Saves, Cheats & Game Extras on the DESR:" "" \
  "PS2 (OPL): <GAME-ID>_0.bin / <GAME-ID>_1.bin (slot 1 / 2), e.g. SLUS_203.12_0.bin;" \
  "           raw 8-64 MiB OPL cards, or PCSX2 .ps2 cards (converted)." \
  "PS2 saves: .psu files, copied onto a real memory card in the DESR." \
  "PS1:       <GAME-ID>.VMC / .mcr / .mcd / .gme / .vmp (slot 1; add _1 for slot 2)," \
  "           single saves <GAME-ID>.mcs (added to the game's card)." > "$P/PC/udpfsd/VMC/_put_memory_cards_here.txt"
printf '%s\n' "Cheats, installed with the game or from Saves, Cheats & Game Extras:" "" \
  "PS2 (OPL):        <GAME-ID>.cht, e.g. SLUS_203.12.cht (switched on for that game;" \
  "                  all codes apply when it is started from the XMB)." \
  "PS1 (POPStarter): <GAME-ID>.txt, e.g. SLUS_005.94.txt (becomes CHEATS.TXT)." > "$P/PC/udpfsd/CHT/_put_cheats_here.txt"
printf '%s\n' "Homebrew apps (.ELF) to install as XMB channels (Apps menu on the DESR)." "" \
  "One app per folder, e.g. APPS\wLaunchELF\BOOT.ELF with the files it needs;" \
  "the installer offers to copy the whole folder. A lone .ELF works too." > "$P/PC/udpfsd/APPS/_put_apps_here.txt"
printf '%s\n' "PS1 games: put .VCD files here, or BIN/CUE (served as .VCD automatically)." \
  "Multi-disc games: name the files ... (Disc 1).VCD, ... (Disc 2).VCD and install disc 1." "" \
  "POPSTARTER.KELF (POPStarter rev13 Beta) is already here." "" \
  "Also needed here, NOT included (Sony's POPS emulator, supply your own):" \
  "  POPS.ELF" \
  "  IOPRP252.IMG" > "$P/PC/udpfsd/POPS/_put_PS1_VCD_games_here.txt"
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
