#!/usr/bin/env bash
# Copy the pinned POPSTARTER.KELF (tools/popstarter.env) to <out file>,
# verified against POPSTARTER_SHA256.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1
. "$ROOT/tools/popstarter.env"
SRC=$ROOT/$POPSTARTER_FILE
[ -f "$SRC" ] || { echo "ERROR: $POPSTARTER_FILE missing"; exit 1; }
echo "$POPSTARTER_SHA256  $SRC" | sha256sum -c --quiet - ||
  { echo "ERROR: $POPSTARTER_FILE does not match POPSTARTER_SHA256"; exit 1; }
mkdir -p "$(dirname "$OUT")"
cp "$SRC" "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
echo "fetch-popstarter: POPStarter $POPSTARTER_VERSION -> $OUT"
