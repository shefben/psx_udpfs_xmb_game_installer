#!/usr/bin/env python3
"""Sanity checks for embedded assets, fixtures and signed payloads.

  verify-assets.py            check assets/ and test/fixtures/
  verify-assets.py --kelf F   check that F looks like a signed KELF

Mirrors the on-console checks in src/hdl_header.c (png_basic_valid,
kelf_looks_valid) so a bad file is caught at build time.
"""
import os
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def check_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "%s: not a PNG" % path
    n, t = struct.unpack(">I4s", d[8:16])
    assert n == 13 and t == b"IHDR", "%s: first chunk is not IHDR" % path
    w, h, depth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", d[16:29])
    assert 1 <= w <= 1024 and 1 <= h <= 1024, "%s: bad size" % path
    assert inter == 0, "%s: interlaced PNG" % path
    # Walk chunks and verify CRCs.
    off = 8
    while off < len(d):
        n, t = struct.unpack(">I4s", d[off:off + 8])
        body = d[off + 8:off + 8 + n]
        crc, = struct.unpack(">I", d[off + 8 + n:off + 12 + n])
        assert zlib.crc32(t + body) & 0xFFFFFFFF == crc, "%s: bad CRC in %r" % (path, t)
        off += 12 + n
        if t == b"IEND":
            break
    return "%dx%d depth %d type %d" % (w, h, depth, ctype)


def check_kelf(path):
    d = open(path, "rb").read()
    assert len(d) >= 1024, "%s: too small for a KELF" % path
    assert d[:4] != b"\x7fELF", "%s: is a plain ELF, not a KELF" % path
    return "%d bytes" % len(d)


def main(argv):
    if len(argv) == 3 and argv[1] == "--kelf":
        print("kelf ok:", argv[2], check_kelf(argv[2]))
        return 0
    for rel, size in (("assets/game/default_jkt_001.png", "140x200"),
                      ("assets/game/default_jkt_002.png", "74x108"),
                      ("assets/installer/jkt_001.png", "140x200"),
                      ("assets/installer/jkt_002.png", "74x108"),
                      ("assets/manual/blank.png", "640x350"),
                      ("assets/manual/jkt_cp.png", "290x46")):
        info = check_png(os.path.join(ROOT, rel))
        assert info.startswith(size + " "), "%s: %s, expected %s" % (rel, info, size)
        print("png ok:", rel, info)
    fx = open(os.path.join(ROOT, "test/fixtures/ppaa_hdldump.bin"), "rb").read()
    assert len(fx) == 2048 and fx[:9] == b"PS2ICON3D", "PPAA fixture invalid"
    print("fixture ok: test/fixtures/ppaa_hdldump.bin")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except AssertionError as e:
        sys.stderr.write("verify-assets: %s\n" % e)
        sys.exit(1)
