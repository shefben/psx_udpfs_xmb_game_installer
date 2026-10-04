#!/usr/bin/env bash
# Clone (or update) the upstream reference repositories into reference/.
# Records the exact revisions used into reference/REVISIONS.txt.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$here/reference"
cd "$here/reference"

repos=(
    pcm720/udpfsd
    ps2max32/neutrino
    ps2homebrew/OPL-Launcher
    ps2homebrew/HDLGameInstaller
    binkynz/ps2-usbhdl
    ps2homebrew/hdl-dump
)

for r in "${repos[@]}"; do
    n="${r#*/}"
    if [ ! -d "$n/.git" ]; then
        rm -rf "$n"
        git clone --quiet "https://github.com/$r.git" "$n"
    fi
done

: > REVISIONS.txt
for r in "${repos[@]}"; do
    n="${r#*/}"
    printf '%-20s %s %s https://github.com/%s\n' "$n" \
        "$(git -C "$n" rev-parse HEAD)" "$(git -C "$n" log -1 --format=%cI)" "$r" >> REVISIONS.txt
done
cat REVISIONS.txt
