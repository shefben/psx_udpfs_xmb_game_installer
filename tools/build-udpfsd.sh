#!/usr/bin/env bash
# Build the patched udpfsd server from the pinned reference.
#
#   tools/build-udpfsd.sh <out dir> [test]
#
# Copies reference/udpfsd (pinned in reference/REVISIONS.txt) to
# build/udpfsd-src, applies patches/udpfsd/*.patch, runs the Go tests of
# the patched packages, and builds Windows and Linux x86-64 binaries
# (CGO off: ISO/CSO/ZSO only, like upstream's release binaries).
# Uses a local `go` >= 1.25 if present, otherwise the pinned Docker image.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1
MODE=${2:-build}
GO_IMAGE=golang:1.25@sha256:699337d620559a59b4a2bb298ad59611e535d2ee755a34cf2d2a98f37578dc80
SRC=$ROOT/build/udpfsd-src

[ -d "$ROOT/reference/udpfsd/.git" ] || { echo "reference/udpfsd missing: run 'make references'"; exit 1; }
rm -rf "$SRC"
mkdir -p "$SRC" "$OUT"
git -C "$ROOT/reference/udpfsd" archive HEAD | tar -x -C "$SRC"
for p in $(ls "$ROOT"/patches/udpfsd/*.patch | sort); do
  patch -d "$SRC" -p1 --forward --no-backup-if-mismatch < "$p" >/dev/null
done
VERSION="$(git -C "$ROOT/reference/udpfsd" rev-parse --short=12 HEAD)+install-dir"

SCRIPT='set -e
export CGO_ENABLED=0 GOFLAGS=-buildvcs=false
go vet ./internal/fs/ ./cmd/...
go test ./internal/fs/ ./internal/fs/compression/zso/ ./internal/fs/compression/cso/
if [ "$MODE" = build ]; then
  GOOS=windows GOARCH=amd64 go build -trimpath -ldflags "-w -s -X main.Version=$VERSION" -o out/udpfsd-windows-amd64.exe ./cmd/udpfsd
  GOOS=linux GOARCH=amd64 go build -trimpath -ldflags "-w -s -X main.Version=$VERSION" -o out/udpfsd-linux-amd64 ./cmd/udpfsd
fi'

mkdir -p "$SRC/out"
if command -v go >/dev/null 2>&1; then
  (cd "$SRC" && MODE=$MODE VERSION=$VERSION sh -c "$SCRIPT")
else
  if command -v docker >/dev/null 2>&1; then DOCKER=docker; MNT=$SRC
  elif command -v docker.exe >/dev/null 2>&1; then DOCKER=docker.exe; MNT=$(wslpath -w "$SRC")
  else echo "ERROR: neither go nor docker found"; exit 1; fi
  "$DOCKER" run --rm -v "$MNT:/src" -w /src -e MODE="$MODE" -e VERSION="$VERSION" "$GO_IMAGE" sh -c "$SCRIPT"
fi
if [ "$MODE" = build ]; then
  cp "$SRC"/out/udpfsd-windows-amd64.exe "$SRC"/out/udpfsd-linux-amd64 "$OUT"/
  echo "udpfsd $VERSION -> $OUT"
fi
