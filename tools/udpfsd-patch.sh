#!/usr/bin/env bash
# Work on udpfsd patches in build/udpfsd-dev.
#
#   tools/udpfsd-patch.sh init <N>     pinned reference + patches numbered < N,
#                                      committed as the dev tree's base
#   tools/udpfsd-patch.sh test         gofmt + go vet + go test (all packages
#                                      except chd, which needs CGO)
#   tools/udpfsd-patch.sh fmt          gofmt -w the packages the patches touch
#   tools/udpfsd-patch.sh save <file>  write the dev tree's diff against its
#                                      base to patches/udpfsd/<file>
#
# Needs a native Go >= 1.25 (as tools/build-udpfsd.sh); no Docker.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEV=$ROOT/build/udpfsd-dev

[ -x /usr/local/go/bin/go ] && PATH=/usr/local/go/bin:$PATH
run_go() {
  command -v go >/dev/null 2>&1 || {
    echo "ERROR: Go 1.25 is needed: install the go1.25.x linux-amd64 tarball from https://go.dev/dl/ into /usr/local/go"
    exit 1
  }
  (cd "$DEV" && sh -c "$1")
}

case "${1:-}" in
  init)
    [ -n "${2:-}" ] || { echo "usage: $0 init <N>"; exit 2; }
    rm -rf "$DEV"
    mkdir -p "$DEV"
    git -C "$ROOT/reference/udpfsd" archive HEAD | tar -x -C "$DEV"
    for p in $(ls "$ROOT"/patches/udpfsd/*.patch 2>/dev/null | sort); do
      n=$(basename "$p" | cut -c1-4)
      if [ "$((10#$n))" -lt "$((10#$2))" ]; then
        patch -d "$DEV" -p1 --no-backup-if-mismatch < "$p" >/dev/null
      fi
    done
    (cd "$DEV" && git init -q && git add -A &&
      git -c user.name=base -c user.email=base@local commit -qm base)
    echo "dev tree ready: $DEV"
    ;;
  test)
    run_go 'set -e
export CGO_ENABLED=0 GOFLAGS=-buildvcs=false
dirs=""
for d in internal/config internal/prep internal/fs cmd/udpfsd; do [ -d "$d" ] && dirs="$dirs $d"; done
bad=$(gofmt -l $dirs)
if [ -n "$bad" ]; then echo "gofmt needed:"; echo "$bad"; exit 1; fi
P=$(go list ./... | grep -v /chd)
go vet $P
go test $P'
    ;;
  fmt)
    run_go 'for d in internal/config internal/prep internal/fs cmd/udpfsd; do [ -d "$d" ] && gofmt -w "$d"; done; true'
    ;;
  save)
    [ -n "${2:-}" ] || { echo "usage: $0 save <file>"; exit 2; }
    (cd "$DEV" && git add -A && git diff --cached > "$ROOT/patches/udpfsd/$2")
    echo "wrote patches/udpfsd/$2 ($(grep -c '^+++ ' "$ROOT/patches/udpfsd/$2") files)"
    ;;
  *)
    echo "usage: $0 init <N> | test | fmt | save <file>"; exit 2
    ;;
esac
