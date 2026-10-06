#!/bin/sh
# Legacy build of HDLGameInstaller's ps2hdd-hdl.irx. Runs INSIDE the
# pinned image ps2dev/ps2dev:v1.0 (the image HDLGameInstaller's own CI
# pins; IOP GCC 3.2.3), invoked by tools/driver/make-driver.sh.
#
#   build-ps2hdd-hdl.sh <apa-hdl dir> <ps2sdk source dir> <out.irx> [<policy.h> <patch>...]
set -eu
SRC=$1
SDKSRC=$2
OUT=$3
POLICY=${4:-}
W=$(mktemp -d)
cp -r "$SRC" "$W/apa-hdl"
if [ -n "$POLICY" ]; then
  cp "$POLICY" "$W/apa-hdl/src/remove_policy.h"
  shift 4
  for p in "$@"; do (cd "$W" && patch -p1 --no-backup-if-mismatch < "$p"); done
fi
make -C "$W/apa-hdl" PS2SDKSRC="$SDKSRC" >"$W/build.log" 2>&1 || { tail -30 "$W/build.log"; exit 1; }
IRX=$(find "$W/apa-hdl" -name 'ps2hdd-hdl.irx' | head -1)
[ -n "$IRX" ] || { echo "no ps2hdd-hdl.irx produced"; tail -30 "$W/build.log"; exit 1; }
cp "$IRX" "$OUT"
rm -rf "$W"
