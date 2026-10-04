#!/usr/bin/env python3
"""Allow ps2hdd-hdl.irx to remove "__" partitions (one-word patch).

HDLGameInstaller's apa-hdl driver (ec37c81, apa-hdl/src/hdd_fio.c,
apaRemove) refuses to remove any partition whose name starts with "__":

    if (id[0] == '_' && id[1] == '_')
        return -EACCES;

Hidden game partitions are named "__.<ID>..<TITLE>", so without this
patch an installed game can never be deleted and an interrupted copy
can never be cleaned up from the console. The check compiles to

    0xdf8  82240000  lb    a0,0(s1)      ; id[0]
    0xdfc  2402005f  li    v0,0x5f       ; '_'
    0xe00  10820042  beq   a0,v0,...     ; -> id[1] check -> -EACCES

The patch changes only the constant at 0xdfc to 0x100. `lb` sign-
extends to -128..127, so the comparison can never match and removal
proceeds as for any other partition. Nothing else in the binary
changes; rename protection for "__" names is untouched.

System partitions (__mbr, __common, __system, ...) are protected by the
installer itself: src/hdd_partitions.c hdd_remove_exact() only removes
names accepted by partition_remove_allowed() (game pairs and
PP.UDPFS-INSTALLER).

  patch-ps2hdd-hdl.py <in.irx> <out.irx>
"""
import hashlib
import struct
import sys

IN_SHA256 = "58b217e93ebaf8b452d948fbde64a324ade6992484e22cc76b9d7aa99b547d15"
EXPECT = {0xdf8: 0x82240000, 0xdfc: 0x2402005f, 0xe00: 0x10820042}
PATCH_OFF, PATCH_WORD = 0xdfc, 0x24020100  # li v0,0x100


def word(d, off):
    return struct.unpack_from("<I", d, off)[0]


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    d = bytearray(open(sys.argv[1], "rb").read())
    h = hashlib.sha256(d).hexdigest()
    if h != IN_SHA256:
        sys.stderr.write("patch-ps2hdd-hdl: unexpected input %s (sha256 %s)\n" % (sys.argv[1], h))
        return 1
    for off, w in EXPECT.items():
        if word(d, off) != w:
            sys.stderr.write("patch-ps2hdd-hdl: word at 0x%x is 0x%08x, expected 0x%08x\n"
                             % (off, word(d, off), w))
            return 1
    struct.pack_into("<I", d, PATCH_OFF, PATCH_WORD)
    with open(sys.argv[2], "wb") as f:
        f.write(d)
    print("patch-ps2hdd-hdl: %s -> %s (sha256 %s)"
          % (sys.argv[1], sys.argv[2], hashlib.sha256(d).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
