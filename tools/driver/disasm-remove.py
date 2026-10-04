#!/usr/bin/env python3
"""Disassemble every "__" name check in a ps2hdd-hdl.irx.

Prints a window around each comparison against '_' (li rX,95) in .text.
In the driver these are apaRemove (the remove policy) and hddReName
(rename protection, unchanged by this project), plus fioGetInput's
"__" test for partition creation. Output goes to
docs/driver/apaRemove-{upstream,patched}.txt.

  disasm-remove.py <ps2hdd-hdl.irx>
"""
import os
import re
import struct
import subprocess
import sys

OBJDUMP = os.environ.get("IOP_OBJDUMP", "mipsel-none-elf-objdump")


def text_section(d):
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    for i in range(shnum):
        _, typ, flags, _, off, size = struct.unpack_from("<IIIIII", d, shoff + i * shentsize)
        if typ == 1 and flags & 4:
            return off, size
    raise SystemExit("no .text")


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    off, size = text_section(d)
    out = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", "mips:3000", "-EL",
                          "--start-address=%d" % off, "--stop-address=%d" % (off + size), path],
                         capture_output=True, text=True, check=True).stdout
    lines = []
    for ln in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(.*)", ln)
        if m:
            lines.append((int(m.group(1), 16), m.group(2), " ".join(m.group(3).split())))
    sites = [i for i, (_, _, ins) in enumerate(lines) if re.match(r"li \w+,95$", ins)]
    import hashlib
    print("# %s" % os.path.basename(path))
    print("# sha256 %s, %d bytes; addresses are file offsets" % (hashlib.sha256(d).hexdigest(), len(d)))
    for n, i in enumerate(sites):
        print("\n## '_' comparison site %d" % (n + 1))
        for a, w, ins in lines[max(0, i - 3):i + 22]:
            print("%06x  %s  %s" % (a, w, ins))


if __name__ == "__main__":
    main()
