#!/usr/bin/env bash
# Build the patched udpfsd server from the pinned reference, with CHD support.
#
#   tools/build-udpfsd.sh <out dir> [test]
#
# Copies reference/udpfsd (pinned in reference/REVISIONS.txt) to
# build/udpfsd-src and applies patches/udpfsd/*.patch.
#
# CHD needs CGO and libchdr. The libchdr commit pinned in tools/libchdr.env is
# fetched with git into build/libchdr-<commit>/src and built with cmake as
# static code only (its bundled lzma/miniz/zstd, no system libraries), merged
# into one libchdr.a per target so the chd package's `-lchdr` pulls in
# everything: linux-amd64 with gcc, windows-amd64 with mingw-w64. Built libs
# are cached in build/libchdr-<commit>/<target>-<options hash>/lib.
#
# go vet and go test run with CGO on Linux for every package, chd included.
# Build mode then makes
#   udpfsd-linux-amd64        fully static (glibc, netgo/osusergo), and
#   udpfsd-windows-amd64.exe  cross-compiled with mingw-w64, libgcc and
#                             libchdr linked in, so it imports Windows system
#                             DLLs only,
# checks both, and copies them to <out dir>.
#
# Runs natively, no Docker. Needs Go 1.25 (`go` on PATH, or /usr/local/go/bin/go)
# and these apt packages (Ubuntu 22.04):
#   build-essential cmake git file gcc-mingw-w64-x86-64
# (mingw-w64 and file are only needed in build mode).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1
MODE=${2:-build}
SRC=$ROOT/build/udpfsd-src
APT_PKGS="build-essential cmake git file gcc-mingw-w64-x86-64"

[ -x /usr/local/go/bin/go ] && PATH=/usr/local/go/bin:$PATH
command -v go >/dev/null 2>&1 || {
  echo "ERROR: Go 1.25 is needed to build udpfsd: install the go1.25.x linux-amd64 tarball from https://go.dev/dl/ into /usr/local/go"
  exit 1
}
case "$(go env GOVERSION)" in
  go1.25|go1.25.*) ;;
  *) echo "ERROR: Go 1.25 is needed to build udpfsd, found $(go env GOVERSION)"; exit 1 ;;
esac

WIN_CC=x86_64-w64-mingw32-gcc
WIN_AR=x86_64-w64-mingw32-ar
WIN_OBJDUMP=x86_64-w64-mingw32-objdump
need="gcc ar make patch cmake git"
[ "$MODE" = build ] && need="$need file $WIN_CC $WIN_AR $WIN_OBJDUMP"
for t in $need; do
  command -v "$t" >/dev/null 2>&1 || {
    echo "ERROR: $t not found. udpfsd with CHD support needs the apt packages: $APT_PKGS"
    echo "       (sudo apt-get install -y $APT_PKGS)"
    exit 1
  }
done

[ -d "$ROOT/reference/udpfsd/.git" ] || { echo "reference/udpfsd missing: run 'make references'"; exit 1; }

# ---- libchdr (pinned, static) ----------------------------------------------
. "$ROOT/tools/libchdr.env"
CHDR=$ROOT/build/libchdr-${LIBCHDR_COMMIT:0:12}

fetch_libchdr() {
  local s=$CHDR/src
  if [ "$(git -C "$s" rev-parse HEAD 2>/dev/null)" = "$LIBCHDR_COMMIT" ] &&
     [ -z "$(git -C "$s" status --porcelain 2>/dev/null)" ]; then
    return
  fi
  echo "libchdr: fetching $LIBCHDR_COMMIT"
  rm -rf "$s" "$s.tmp"
  mkdir -p "$s.tmp"
  git -C "$s.tmp" init -q
  git -C "$s.tmp" fetch -q --depth 1 "$LIBCHDR_URL" "$LIBCHDR_COMMIT"
  git -C "$s.tmp" -c advice.detachedHead=false checkout -q FETCH_HEAD
  [ "$(git -C "$s.tmp" rev-parse HEAD)" = "$LIBCHDR_COMMIT" ] || {
    echo "ERROR: libchdr checkout is $(git -C "$s.tmp" rev-parse HEAD), expected $LIBCHDR_COMMIT"
    exit 1
  }
  mv "$s.tmp" "$s"
}

# build_libchdr <target> <cc> <ar> [cmake args...]: prints the lib directory
# holding libchdr.a (libchdr + its bundled lzma, miniz and zstd decoders).
build_libchdr() {
  local target=$1 cc=$2 ar=$3; shift 3
  local opts=(-G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF -DCHDR_WANT_TESTS=OFF
    -DWITH_SYSTEM_ZLIB=OFF -DWITH_SYSTEM_ZSTD=OFF -DWITH_LZMA_ASM=OFF "$@")
  local key dir
  key=$(printf '%s\n' "$LIBCHDR_COMMIT" "${opts[@]}" "$("$cc" --version | sed -n 1p)" | sha256sum | cut -c1-8)
  dir=$CHDR/$target-$key
  if [ ! -f "$dir/lib/libchdr.a" ]; then
    echo "libchdr: building $target (log: ${dir#$ROOT/}.log)" >&2
    rm -rf "$dir" "$dir.tmp"
    mkdir -p "$dir.tmp/lib" "$dir.tmp/objs"
    if ! (cmake -S "$CHDR/src" -B "$dir.tmp/cmake" "${opts[@]}" &&
          cmake --build "$dir.tmp/cmake" -j"$(nproc)" --target chdr-static chdr-lzma miniz zstd
         ) >"$dir.log" 2>&1; then
      tail -n 40 "$dir.log" >&2
      echo "ERROR: libchdr build for $target failed (see ${dir#$ROOT/}.log)" >&2
      exit 1
    fi
    local libs=() n a i=0
    for n in chdr-static chdr-lzma miniz zstd; do
      a=$(find "$dir.tmp/cmake" -name "lib$n.a" -print -quit)
      [ -n "$a" ] || { echo "ERROR: libchdr: lib$n.a was not built for $target" >&2; exit 1; }
      libs+=("$a")
    done
    for a in "${libs[@]}"; do
      i=$((i + 1))
      mkdir "$dir.tmp/objs/$i"
      (cd "$dir.tmp/objs/$i" && "$ar" x "$a")
    done
    "$ar" qcsD "$dir.tmp/lib/libchdr.a" "$dir.tmp"/objs/*/*
    rm -rf "$dir.tmp/objs" "$dir.tmp/cmake"
    mv "$dir.tmp" "$dir"
  fi
  echo "$dir/lib"
}

fetch_libchdr
CHDR_LINUX=$(build_libchdr linux-amd64 gcc ar)
if [ "$MODE" = build ]; then
  CHDR_WIN=$(build_libchdr windows-amd64 "$WIN_CC" "$WIN_AR" \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=AMD64)
fi

# ---- udpfsd ------------------------------------------------------------------
rm -rf "$SRC"
mkdir -p "$SRC" "$OUT"
OUT=$(cd "$OUT" && pwd)
git -C "$ROOT/reference/udpfsd" archive HEAD | tar -x -C "$SRC"
for p in $(ls "$ROOT"/patches/udpfsd/*.patch | sort); do
  patch -d "$SRC" -p1 --forward --no-backup-if-mismatch < "$p" >/dev/null
done
VERSION="$(git -C "$ROOT/reference/udpfsd" rev-parse --short=12 HEAD)+game-prep-v3.2"

mkdir -p "$SRC/out"
cd "$SRC"
export GOFLAGS=-buildvcs=false CGO_ENABLED=1
CC=gcc CGO_LDFLAGS="-L$CHDR_LINUX" go vet ./...
CC=gcc CGO_LDFLAGS="-L$CHDR_LINUX" go test ./...
if [ "$MODE" = build ]; then
  LDFLAGS="-w -s -X main.Version=$VERSION -linkmode external -extldflags -static"
  GOOS=linux GOARCH=amd64 CC=gcc CGO_LDFLAGS="-L$CHDR_LINUX" \
    go build -trimpath -tags netgo,osusergo -ldflags "$LDFLAGS" -o out/udpfsd-linux-amd64 ./cmd/udpfsd
  GOOS=windows GOARCH=amd64 CC=$WIN_CC CGO_LDFLAGS="-L$CHDR_WIN" \
    go build -trimpath -ldflags "$LDFLAGS" -o out/udpfsd-windows-amd64.exe ./cmd/udpfsd

  # (outputs are captured first: grep -q closing a pipe early would fail
  # the check under pipefail)
  info=$(file -b out/udpfsd-linux-amd64)
  case "$info" in
    *"statically linked"*) ;;
    *) echo "ERROR: out/udpfsd-linux-amd64 is not statically linked: $info"; exit 1 ;;
  esac
  if [ "$(uname -m)" = x86_64 ]; then
    usage=$(out/udpfsd-linux-amd64 -h 2>&1 || true)
    grep -q 'decompression for .*CHD' <<<"$usage" || {
      echo "ERROR: out/udpfsd-linux-amd64 was built without CHD support"
      exit 1
    }
  fi
  dlls=$("$WIN_OBJDUMP" -p out/udpfsd-windows-amd64.exe | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r')
  bad=$(printf '%s\n' "$dlls" | grep -iE '^(lib|zlib)' || true)
  [ -z "$bad" ] || {
    echo "ERROR: udpfsd-windows-amd64.exe imports non-system DLLs:" $bad
    exit 1
  }
  cp out/udpfsd-windows-amd64.exe out/udpfsd-linux-amd64 "$OUT"/
  echo "udpfsd $VERSION (CHD: libchdr ${LIBCHDR_COMMIT:0:12}) -> $OUT"
fi
