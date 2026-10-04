#!/usr/bin/env bash
# Tests for tools/kelf-sign.sh against a fake kelftool. No real keys.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SIGN=$ROOT/tools/kelf-sign.sh
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
chmod +x "$ROOT/test/host/fake-kelftool"
export KELFTOOL=$ROOT/test/host/fake-kelftool
export FAKE_KELF_LOG=$W/log
printf 'not real keys' > "$W/PS2KEYS.dat"
{ printf '\x7fELF'; head -c 4000 /dev/urandom; } > "$W/in.elf"
printf 'hello' > "$W/notelf.bin"

pass=0 fail=0
check() { # name, condition result (0 = ok)
  if [ "$2" -eq 0 ]; then pass=$((pass + 1)); echo "ok   kelf_sign_$1"
  else fail=$((fail + 1)); echo "FAIL kelf_sign_$1"; fi
}
no_outputs() { [ ! -e "$W/out.KELF" ] && [ ! -e "$W/out.KELF.tmp" ] && [ ! -e "$W/out.KELF.verify.elf" ]; }
run() { env "$@" bash "$SIGN" "$W/in.elf" "$W/out.KELF" >"$W/stdout" 2>"$W/stderr"; }

# 1. PS2KEYS missing -> precise error, nothing written
rm -f "$W"/out.KELF*; unset PS2KEYS
run FAKE_KELF=ok; rc=$?
[ $rc -ne 0 ] && grep -q "PS2KEYS is not set" "$W/stderr" && no_outputs; check missing_keys $?

# 2. relative PS2KEYS rejected
run PS2KEYS=PS2KEYS.dat FAKE_KELF=ok; rc=$?
[ $rc -ne 0 ] && grep -q "absolute path" "$W/stderr" && no_outputs; check relative_keys $?

# 3. unknown KELF_MODE rejected
run PS2KEYS=$W/PS2KEYS.dat KELF_MODE=fmcb FAKE_KELF=ok; rc=$?
[ $rc -ne 0 ] && grep -q "KELF_MODE must be" "$W/stderr" && no_outputs; check bad_mode $?

# 4. success, default mode is mbr
: > "$W/log"
run PS2KEYS=$W/PS2KEYS.dat FAKE_KELF=ok; rc=$?
[ $rc -eq 0 ] && [ -s "$W/out.KELF" ] && [ ! -e "$W/out.KELF.tmp" ] && \
  grep -q "^encrypt mbr " "$W/log" && grep -q "^decrypt " "$W/log"; check success_mbr $?

# 5. explicit none mode
: > "$W/log"
run PS2KEYS=$W/PS2KEYS.dat KELF_MODE=none FAKE_KELF=ok; rc=$?
[ $rc -eq 0 ] && grep -q "^encrypt $W/in.elf " "$W/log" && grep -q WARNING "$W/stderr"; check mode_none $?

# 6-10. failures: no output left behind, and a stale good output is removed
for mode in fail-encrypt empty plain-elf tiny fail-decrypt bad-decrypt; do
  run PS2KEYS=$W/PS2KEYS.dat FAKE_KELF=ok >/dev/null   # leave a good stale file
  run PS2KEYS=$W/PS2KEYS.dat FAKE_KELF=$mode; rc=$?
  [ $rc -ne 0 ] && no_outputs; check "failure_${mode//-/_}" $?
done

# 11. input must be an ELF
env PS2KEYS=$W/PS2KEYS.dat bash "$SIGN" "$W/notelf.bin" "$W/out.KELF" >/dev/null 2>&1; rc=$?
[ $rc -ne 0 ] && no_outputs; check input_not_elf $?

# 12. keys are linked, never copied into the output directory
ls "$W" | grep -qv '^PS2KEYS.dat$' ; [ "$(find "$W" -name 'PS2KEYS*' | wc -l)" -eq 1 ]; check keys_not_copied $?

echo
echo "kelf-sign: $pass passed, $fail failed"
[ $fail -eq 0 ]
