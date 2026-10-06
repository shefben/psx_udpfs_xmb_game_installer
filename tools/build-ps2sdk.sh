#!/usr/bin/env bash
# Build the pinned PS2SDK (tools/ps2sdk.env) with the installed PS2DEV
# toolchain and install it into PS2SDK_PREFIX. The existing
# $PS2DEV/ps2sdk is left alone. Re-running with the same revision is a
# no-op.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/ps2sdk.env"
export PS2DEV="${PS2DEV:-/usr/local/ps2dev/ps2dev}"
STAMP=$PS2SDK_PREFIX/.psx-installer-rev
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$PS2SDK_REV" ]; then
  echo "build-ps2sdk: $PS2SDK_PREFIX is already $PS2SDK_REV"
  exit 0
fi
SRC=${PS2SDK_SRC_CACHE:-$HOME/.cache/psx-installer/ps2sdk-src}
if [ ! -d "$SRC/.git" ]; then
  mkdir -p "$(dirname "$SRC")"
  git clone --quiet "$PS2SDK_REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$PS2SDK_REV" 2>/dev/null || git -C "$SRC" fetch --quiet origin
git -C "$SRC" checkout --quiet --force "$PS2SDK_REV"
git -C "$SRC" clean -fdxq
export PS2SDK=$PS2SDK_PREFIX
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
rm -rf "$PS2SDK_PREFIX"
mkdir -p "$PS2SDK_PREFIX"
make -C "$SRC" -j"$(nproc)" >/dev/null
make -C "$SRC" release >/dev/null
grep -q "$PS2SDK_ATAD_LBA48_MARK" "$PS2SDK_PREFIX/iop/irx/ps2atad.irx" ||
  { echo "ERROR: built ps2atad.irx has no LBA48 DVRP support"; exit 1; }
echo "$PS2SDK_REV" > "$STAMP"
echo "build-ps2sdk: PS2SDK $PS2SDK_REV -> $PS2SDK_PREFIX"
