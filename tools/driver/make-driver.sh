#!/usr/bin/env bash
# Reproduce HDLGameInstaller's ps2hdd-hdl.irx from source, then build the
# patched variant this project ships (vendor/irx/ps2hdd-hdl.irx).
#
# Needs Docker and network access (pulls ps2dev/ps2dev:v1.0 once).
# Step 1 must reproduce the vendored upstream binary byte-for-byte or the
# script stops: the patched driver is only trusted because its unpatched
# build is identical to the hardware-proven one.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/tools/driver/driver.env"
. "$ROOT/tools/ps2env.sh" # mipsel-none-elf-objdump for the evidence files
IMAGE=ps2dev/ps2dev:v1.0

HDLGI=$ROOT/reference/HDLGameInstaller
PS2SDK_GIT=${PS2SDK_GIT:-$ROOT/reference/ps2sdk}
[ -d "$HDLGI/.git" ] || { echo "reference/HDLGameInstaller missing (make references)"; exit 1; }
if [ ! -d "$PS2SDK_GIT/.git" ]; then
  echo "cloning ps2sdk history into $PS2SDK_GIT"
  git clone --quiet https://github.com/ps2dev/ps2sdk.git "$PS2SDK_GIT"
fi

W=$(mktemp -d "$ROOT/build/driver.XXXXXX" 2>/dev/null || { mkdir -p "$ROOT/build"; mktemp -d "$ROOT/build/driver.XXXXXX"; })
trap 'rm -rf "$W"' EXIT
git -C "$HDLGI" archive "$DRIVER_SRC_REV" apa-hdl | tar -x -C "$W"
mkdir -p "$W/sdk"
git -C "$PS2SDK_GIT" archive "$DRIVER_PS2SDK_REV" | tar -x -C "$W/sdk"
cp "$ROOT/tools/driver/build-ps2hdd-hdl.sh" "$ROOT/tools/driver/remove_policy.h" \
   "$ROOT"/patches/apa-hdl/*.patch "$W/"

# Under WSL without the Docker integration, use Docker Desktop's
# Windows CLI with a Windows path for the bind mount.
if command -v docker >/dev/null 2>&1; then DOCKER=docker; MNT=$W
elif command -v docker.exe >/dev/null 2>&1; then DOCKER=docker.exe; MNT=$(wslpath -w "$W")
else echo "ERROR: docker not found"; exit 1; fi

"$DOCKER" run --rm -v "$MNT:/w" "$IMAGE" sh -c '
  set -e
  apk add --no-cache make patch >/dev/null
  sh /w/build-ps2hdd-hdl.sh /w/apa-hdl /w/sdk /w/upstream.irx
  sh /w/build-ps2hdd-hdl.sh /w/apa-hdl /w/sdk /w/patched.irx /w/remove_policy.h \
     /w/0001-allow-removing-hidden-hdl-games.patch /w/0002-allow-renaming-hidden-hdl-games.patch
  chmod 666 /w/*.irx'

up=$(sha256sum "$W/upstream.irx" | cut -d' ' -f1)
vend=$(sha256sum "$HDLGI/irx/ps2hdd-hdl.irx" | cut -d' ' -f1)
echo "upstream source build: $up"
echo "vendored upstream IRX: $vend"
[ "$up" = "$vend" ] && [ "$up" = "$DRIVER_UPSTREAM_SHA256" ] || {
  echo "ERROR: source build does not reproduce the vendored driver; refusing to continue"; exit 1; }

mkdir -p "$ROOT/vendor/irx"
cp "$W/patched.irx" "$ROOT/vendor/irx/ps2hdd-hdl.irx"
pat=$(sha256sum "$ROOT/vendor/irx/ps2hdd-hdl.irx" | cut -d' ' -f1)
echo "patched build:         $pat"
if [ "$pat" != "$DRIVER_PATCHED_SHA256" ]; then
  echo "NOTE: patched hash differs from tools/driver/driver.env; update DRIVER_PATCHED_SHA256 if the patch changed"
fi
mkdir -p "$ROOT/docs/driver"
python3 "$ROOT/tools/driver/disasm-remove.py" "$HDLGI/irx/ps2hdd-hdl.irx" > "$ROOT/docs/driver/apaRemove-upstream.txt"
python3 "$ROOT/tools/driver/disasm-remove.py" "$ROOT/vendor/irx/ps2hdd-hdl.irx" > "$ROOT/docs/driver/apaRemove-patched.txt"
echo "wrote vendor/irx/ps2hdd-hdl.irx and docs/driver/apaRemove-*.txt"
