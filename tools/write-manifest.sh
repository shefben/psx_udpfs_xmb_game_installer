#!/usr/bin/env bash
# Write dist/SHA256SUMS and dist/BUILD-MANIFEST.txt.
#   write-manifest.sh <dist> <driver.irx> <neutrino irx dir> <OPL-Launcher.elf> <kelf-mode stamp>
set -euo pipefail
DIST=$1
DRIVER=$2
IRX=$3
OPL_ELF=$4
MODE_STAMP=$5
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/driver/driver.env"
cd "$DIST"
sha256sum desr-udpfs-installer-bootstrap.elf desr-udpfs-installer-app.elf \
  installer-EXECUTE.KELF opl-launcher-EXECUTE.KELF \
  udpfsd/udpfsd-windows-amd64.exe udpfsd/udpfsd-linux-amd64 > SHA256SUMS
{
  echo "build: $(git -C "$ROOT" describe --always --dirty --abbrev=12)"
  echo "commit date: $(git -C "$ROOT" log -1 --format=%cI)"
  echo "KELF_MODE (used to sign the KELFs below): $(cat "$MODE_STAMP")"
  echo
  echo "== dist artifacts"
  cat SHA256SUMS
  echo
  echo "== embedded IOP modules"
  sha256sum "$ROOT/$DRIVER" "$ROOT/reference/HDLGameInstaller/irx/hdlfs.irx" \
    "$IRX"/smap.irx "$IRX"/ministack.irx "$IRX"/udpfs_ioman.irx | sed "s#$ROOT/##"
  echo
  echo "== HDD driver"
  echo "strategy: reproducible legacy source build (tools/driver/), not a binary patch"
  echo "upstream ps2hdd-hdl.irx (vendored, reproduced from source): $DRIVER_UPSTREAM_SHA256"
  echo "shipped  ps2hdd-hdl.irx (patches/apa-hdl/0001):              $DRIVER_PATCHED_SHA256"
  echo
  echo "== OPL-Launcher (unsigned input of opl-launcher-EXECUTE.KELF)"
  sha256sum "$OPL_ELF" | sed "s#$ROOT/##"
  echo
  echo "== udpfsd server"
  echo "pinned upstream + patches/udpfsd/0001-install-dir.patch (adds -install-dir, served as /INSTALL)"
  echo "patch sha256: $(sha256sum "$ROOT/patches/udpfsd/0001-install-dir.patch" | cut -d' ' -f1)"
  echo
  echo "== upstream revisions"
  cat "$ROOT/reference/REVISIONS.txt"
} > BUILD-MANIFEST.txt
echo "dist: $(ls | tr '\n' ' ')"
