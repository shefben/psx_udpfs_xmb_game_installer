#!/usr/bin/env bash
# Clone the upstream reference repositories into reference/ and check
# out EXACTLY the revisions pinned in reference/REVISIONS.txt (committed).
#
#   tools/fetch-references.sh            clone/checkout pinned revisions
#   tools/fetch-references.sh --check    only verify (used by make)
#
# Changing a pin is a deliberate edit of reference/REVISIONS.txt.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
pins="$here/reference/REVISIONS.txt"
mode=${1:-fetch}
[ -f "$pins" ] || { echo "missing $pins"; exit 1; }
mkdir -p "$here/reference"
cd "$here/reference"

bad=0
while read -r name rev _date url; do
  [ -n "$name" ] || continue
  if [ "$mode" = "--check" ]; then
    have=$(git -C "$name" rev-parse HEAD 2>/dev/null || echo missing)
    if [ "$have" != "$rev" ]; then
      echo "reference/$name is at $have, pinned $rev (run: make references)"
      bad=1
    fi
    continue
  fi
  if [ ! -d "$name/.git" ]; then
    rm -rf "$name"
    git clone --quiet "$url.git" "$name"
  fi
  git -C "$name" cat-file -e "$rev^{commit}" 2>/dev/null || git -C "$name" fetch --quiet origin
  git -C "$name" checkout --quiet --detach "$rev"
  [ "$(git -C "$name" rev-parse HEAD)" = "$rev" ] || { echo "checkout of $name failed"; exit 1; }
  echo "$name @ $rev"
done < "$pins"
exit $bad
