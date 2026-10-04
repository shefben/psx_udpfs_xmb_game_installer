#!/usr/bin/env bash
# End-to-end check of the release build graph with a FAKE kelftool, in
# a private BUILD/DIST directory (the real build/ and dist/ are not
# touched). Needs the PS2 toolchain. Run: make test-graph
#
# Checks the required order (OPL-Launcher signed before the app ELF is
# built; app signed before the bootstrap is built), what each ELF
# embeds, the dist/ contents, and that an unchanged tree re-signs
# nothing.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
chmod +x "$ROOT/test/host/fake-kelftool"
printf 'not real keys' > "$W/PS2KEYS.dat"
export KELFTOOL=$ROOT/test/host/fake-kelftool FAKE_KELF_LOG=$W/kelf.log PS2KEYS=$W/PS2KEYS.dat
B=$W/build D=$W/dist
pass=0 fail=0
check() { if [ "$2" -eq 0 ]; then pass=$((pass+1)); echo "ok   graph_$1"; else fail=$((fail+1)); echo "FAIL graph_$1"; fi; }

make -C "$ROOT" dist BUILD="$B" DIST="$D" > "$W/make1.log" 2>&1; rc=$?
check make_dist_succeeds $rc
[ $rc -eq 0 ] || { tail -30 "$W/make1.log"; exit 1; }

# Order: encrypt(OPL) < link(app) < encrypt(app) < link(bootstrap)
L=$W/make1.log
n_opl=$(grep -n "kelf-sign.sh .*OPL-Launcher.elf" "$L" | head -1 | cut -d: -f1)
n_app=$(grep -n -- "-o $B/app/app-debug.elf" "$L" | head -1 | cut -d: -f1)
n_appk=$(grep -n "kelf-sign.sh .*desr-udpfs-installer-app.elf" "$L" | head -1 | cut -d: -f1)
n_boot=$(grep -n -- "-o $B/bootstrap/bootstrap-debug.elf" "$L" | head -1 | cut -d: -f1)
[ -n "$n_opl" ] && [ -n "$n_app" ] && [ -n "$n_appk" ] && [ -n "$n_boot" ] && \
  [ "$n_opl" -lt "$n_app" ] && [ "$n_app" -lt "$n_appk" ] && [ "$n_appk" -lt "$n_boot" ]
check order_opl_app_appkelf_bootstrap $?

contains() { python3 -c "import sys; sys.exit(0 if open(sys.argv[2],'rb').read() in open(sys.argv[1],'rb').read() else 1)" "$1" "$2"; }
contains "$B/app/desr-udpfs-installer-app.elf" "$B/kelf/opl-launcher-EXECUTE.KELF"; check app_embeds_opl_kelf $?
! contains "$B/app/desr-udpfs-installer-app.elf" "$B/kelf/installer-EXECUTE.KELF"; check app_has_no_own_kelf $?
contains "$B/bootstrap/desr-udpfs-installer-bootstrap.elf" "$B/kelf/opl-launcher-EXECUTE.KELF"; check bootstrap_embeds_opl_kelf $?
contains "$B/bootstrap/desr-udpfs-installer-bootstrap.elf" "$B/kelf/installer-EXECUTE.KELF"; check bootstrap_embeds_app_kelf $?
contains "$B/bootstrap/desr-udpfs-installer-bootstrap.elf" "$ROOT/vendor/irx/ps2hdd-hdl.irx"; check bootstrap_embeds_shipped_driver $?
! contains "$B/bootstrap/desr-udpfs-installer-bootstrap.elf" "$ROOT/reference/HDLGameInstaller/irx/ps2hdd-hdl.irx"; check bootstrap_not_upstream_driver $?

for f in desr-udpfs-installer-bootstrap.elf desr-udpfs-installer-app.elf installer-EXECUTE.KELF \
         opl-launcher-EXECUTE.KELF SHA256SUMS BUILD-MANIFEST.txt \
         udpfsd/udpfsd-windows-amd64.exe udpfsd/udpfsd-linux-amd64 \
         udpfsd/opl-launcher-EXECUTE.KELF udpfsd/udpfsd.cfg udpfsd/OPNPS2LD.ELF; do
  [ -s "$D/$f" ] || { echo "missing $f"; false; }
done; check dist_contents $?
(cd "$D" && sha256sum -c SHA256SUMS >/dev/null); check dist_sha256sums $?
! find "$D" "$B" -name '*.tmp' -o -name '*PS2KEYS*' | grep -q .; check no_tmp_or_keys_in_outputs $?

# Rebuild with nothing changed: no re-signing.
: > "$W/kelf.log"
make -C "$ROOT" dist BUILD="$B" DIST="$D" > "$W/make2.log" 2>&1; rc=$?
[ $rc -eq 0 ] && ! grep -q "^encrypt" "$W/kelf.log"; check incremental_no_resign $?

# Switching KELF_MODE re-signs both KELFs, re-embeds, and the manifest
# reports the mode actually used.
: > "$W/kelf.log"
make -C "$ROOT" dist BUILD="$B" DIST="$D" KELF_MODE=none > "$W/make3.log" 2>&1; rc=$?
n_enc=$(grep -c "^encrypt $B" "$W/kelf.log")
n_mbr=$(grep -c "^encrypt mbr $B" "$W/kelf.log")
[ $rc -eq 0 ] && [ "$n_enc" -eq 2 ] && [ "$n_mbr" -eq 0 ] && \
  grep -q "KELF_MODE (used to sign the KELFs below): none" "$D/BUILD-MANIFEST.txt"
r=$?; [ $r -eq 0 ] || { echo "  rc=$rc encrypt=$n_enc mbr=$n_mbr"; tail -5 "$W/make3.log"; }
check kelf_mode_switch_resigns $r
contains "$B/bootstrap/desr-udpfs-installer-bootstrap.elf" "$B/kelf/installer-EXECUTE.KELF"; check bootstrap_reembeds_after_mode_switch $?

# A reference checkout that drifts from its pin stops the build.
cp "$ROOT/reference/REVISIONS.txt" "$W/REVISIONS.good"
sed 's/^OPL-Launcher \+[0-9a-f]\{40\}/OPL-Launcher         0000000000000000000000000000000000000000/' \
  "$W/REVISIONS.good" > "$ROOT/reference/REVISIONS.txt"
make -C "$ROOT" dist BUILD="$B" DIST="$D" > "$W/make4.log" 2>&1; rc=$?
cp "$W/REVISIONS.good" "$ROOT/reference/REVISIONS.txt"
[ $rc -ne 0 ] && grep -q "pinned 0000000000000000000000000000000000000000" "$W/make4.log"
check drifted_reference_stops_build $?

echo
echo "build graph: $pass passed, $fail failed"
[ $fail -eq 0 ]
