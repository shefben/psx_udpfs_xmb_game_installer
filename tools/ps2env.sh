# Source this file to get the PS2DEV toolchain environment.
# Override PS2DEV / PS2SDK before sourcing if yours live elsewhere.
# PS2SDK defaults to the pinned build (tools/ps2sdk.env, `make ps2sdk`),
# else $PS2DEV/ps2sdk (the build then stops: its atad lacks LBA48 support).
export PS2DEV="${PS2DEV:-/usr/local/ps2dev/ps2dev}"
if [ -z "${PS2SDK:-}" ]; then
  _psxi_env="$(dirname "${BASH_SOURCE[0]:-$0}")/ps2sdk.env"
  [ -f "$_psxi_env" ] && . "$_psxi_env"
  if [ -n "${PS2SDK_PREFIX:-}" ] && [ -d "$PS2SDK_PREFIX" ]; then
    export PS2SDK="$PS2SDK_PREFIX"
  else
    export PS2SDK="$PS2DEV/ps2sdk"
  fi
  unset _psxi_env
fi
export GSKIT="${GSKIT:-$PS2DEV/gsKit}"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
