#!/usr/bin/env bash
# Produce test/fixtures/ppaa_hdldump.bin: the first 2 KiB of the
# partition-relative PPAA area (offset 0x1000) of a PFS partition after
# `hdl_dump modify_header` injected our PFS-boot system.cnf.
#
# Needs pfsshell and hdl_dump on PATH. The fixture is checked in, so
# this only has to be re-run if the generated system.cnf changes.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
out="$here/test/fixtures"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
cd "$work"

# Release hdl_dump only opens block devices; a DEBUG build (-D_DEBUG,
# the upstream default) accepts image files. Build it from the pinned
# reference so the fixture is tied to that revision.
cp -r "$here/reference/hdl-dump" hdl-src
make -C hdl-src DEBUG=yes RELEASE=no hdl_dump >hdl-build.log 2>&1 ||
  { tail -20 hdl-build.log; exit 1; }
hdl_dump() { "$work/hdl-src/hdl_dump" "$@"; }

truncate -s 8G disk.img
pfsshell <<EOF >pfsshell.log 2>&1
device disk.img
initialize yes
mkpart PP.SLUS-20312..GRAN_TURISMO_4 128M PFS
exit
EOF

# Exact bytes the installer generates (src/xmb_text.c XMB_SYSTEM_CNF).
printf 'BOOT2 = pfs:/EXECUTE.KELF\nVER = 1.00\nVMODE = NTSC\nHDDUNITPOWER = NICHDD\n' > system.cnf
cp system.cnf "$out/system.cnf"

hdl_dump modify_header disk.img PP.SLUS-20312..GRAN_TURISMO_4 | tee hdl_dump.log

# Locate the partition start sector by walking the APA chain.
start=$(python3 - <<'PY'
import struct
f = open("disk.img", "rb")
sec = 0
seen = set()
while sec not in seen:
    seen.add(sec)
    f.seek(sec * 512)
    h = f.read(512)
    assert h[4:8] == b"APA\0", "bad APA magic at %d" % sec
    nxt, = struct.unpack_from("<I", h, 8)
    pid = h[16:48].split(b"\0")[0].decode()
    # A partition's APA header sits in its own first sector.
    if pid == "PP.SLUS-20312..GRAN_TURISMO_4":
        print(sec)
        break
    sec = nxt
    if sec == 0:
        raise SystemExit("partition not found")
PY
)
dd if=disk.img of="$out/ppaa_hdldump.bin" bs=512 skip=$((start + 8)) count=4 status=none
echo "partition @ sector $start; fixture written:"
xxd -l 0x260 "$out/ppaa_hdldump.bin"
