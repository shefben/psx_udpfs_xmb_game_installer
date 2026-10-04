# Source this file to get the PS2DEV toolchain environment.
# Override PS2DEV before sourcing if your toolchain lives elsewhere.
export PS2DEV="${PS2DEV:-/usr/local/ps2dev/ps2dev}"
export PS2SDK="${PS2SDK:-$PS2DEV/ps2sdk}"
export GSKIT="${GSKIT:-$PS2DEV/gsKit}"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
