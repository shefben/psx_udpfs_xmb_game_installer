#!/usr/bin/env bash
# Extract the pinned OPL runtime (tools/opl.env) to <out dir>:
#   OPNPS2LD.ELF and OPL-LICENSE.txt
# Uses the archive in vendor/opl/ (downloaded from OPL_URL if missing);
# archive and ELF are verified against their pinned SHA-256. Needs 7z.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1
. "$ROOT/tools/opl.env"
ARCHIVE=$ROOT/$OPL_ARCHIVE

if [ ! -f "$ARCHIVE" ]; then
  echo "fetch-opl: downloading $OPL_URL"
  mkdir -p "$(dirname "$ARCHIVE")"
  curl -sfL -o "$ARCHIVE.tmp" "$OPL_URL" || {
    rm -f "$ARCHIVE.tmp"
    echo "ERROR: cannot download $OPL_URL (the 'latest' tag may have moved)."
    echo "       Put $(basename "$ARCHIVE") in vendor/opl/ by hand."
    exit 1
  }
  mv "$ARCHIVE.tmp" "$ARCHIVE"
fi
echo "$OPL_ARCHIVE_SHA256  $ARCHIVE" | sha256sum -c --quiet - ||
  { echo "ERROR: $OPL_ARCHIVE does not match OPL_ARCHIVE_SHA256"; exit 1; }
command -v 7z >/dev/null || { echo "ERROR: 7z not found (apt install p7zip-full)"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
7z x -y -o"$TMP" "$ARCHIVE" >/dev/null
echo "$OPL_ELF_SHA256  $TMP/$OPL_ELF_MEMBER" | sha256sum -c --quiet - ||
  { echo "ERROR: extracted OPL ELF does not match OPL_ELF_SHA256"; exit 1; }
mkdir -p "$OUT"
cp "$TMP/$OPL_ELF_MEMBER" "$OUT/OPNPS2LD.ELF.tmp"
mv "$OUT/OPNPS2LD.ELF.tmp" "$OUT/OPNPS2LD.ELF"
cp "$TMP/OPNPS2LD/LICENSE" "$OUT/OPL-LICENSE.txt"
echo "fetch-opl: OPL $OPL_VERSION -> $OUT/OPNPS2LD.ELF"
