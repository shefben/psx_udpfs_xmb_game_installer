#!/usr/bin/env bash
# PSX/DESR optical drives use the XOSD register/ATAPI backend, not the
# standard PS2 CDVD backend. Build both services from the pinned SDK.
set -euo pipefail
SRC=${1:?PS2SDK source directory required}
DEST=${2:?PS2SDK install directory required}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# XOSD-only bus calls lack declarations/imports in upstream's default
# build. Patch only that variant; the ATAD LBA48 driver stays upstream.
for PATCH in "$ROOT"/patches/ps2sdk/*.patch; do
  if git -C "$SRC" apply --check "$PATCH" 2>/dev/null; then
    git -C "$SRC" apply "$PATCH"
  else
    git -C "$SRC" apply --reverse --check "$PATCH"
  fi
done
for module in cdvdman cdvdfsv; do
  make -C "$SRC/iop/cdvd/$module" PS2SDKSRC="$SRC" clean >/dev/null
  # Upstream XOSD branches have signed char/u8 mismatches and unused
  # variant helpers. Keep all other SDK warnings fatal.
  IOP_CFLAGS=-DCDVD_VARIANT_XOSD make -C "$SRC/iop/cdvd/$module" PS2SDKSRC="$SRC" \
    IOP_WARNFLAGS='-Wall -Werror -Wno-pointer-sign -Wno-unused-function' >/dev/null
  cp "$SRC/iop/cdvd/$module/irx/$module.irx" "$DEST/iop/irx/x$module.irx"
done
