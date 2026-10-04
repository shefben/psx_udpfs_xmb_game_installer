#!/usr/bin/env python3
"""Generate the embedded jacket PNGs (74x108, 8-bit RGB, no interlace).

74x108 is the jacket size PSX-XMB-Manager writes for DESR channels.
The images are deterministic so rebuilding never changes their bytes.
Only the standard library is used (zlib + struct).
"""
import os
import struct
import sys
import zlib

W, H = 74, 108

# 5x7 glyphs, rows top to bottom, 5 bits each (MSB = left).
FONT = {
    "A": [14, 17, 17, 31, 17, 17, 17], "D": [30, 17, 17, 17, 17, 17, 30],
    "E": [31, 16, 16, 30, 16, 16, 31], "F": [31, 16, 16, 30, 16, 16, 16],
    "G": [14, 17, 16, 23, 17, 17, 15], "I": [14, 4, 4, 4, 4, 4, 14],
    "L": [16, 16, 16, 16, 16, 16, 31], "M": [17, 27, 21, 21, 17, 17, 17],
    "N": [17, 25, 21, 19, 17, 17, 17], "P": [30, 17, 17, 30, 16, 16, 16],
    "R": [30, 17, 17, 30, 20, 18, 17], "S": [15, 16, 16, 14, 1, 1, 30],
    "T": [31, 4, 4, 4, 4, 4, 4], "U": [17, 17, 17, 17, 17, 17, 14],
    "2": [14, 17, 1, 2, 4, 8, 31], " ": [0] * 7,
}


def png(pixels):
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def render(top, bottom, lines):
    px = []
    for y in range(H):
        t = y / (H - 1)
        c = [int(top[i] * (1 - t) + bottom[i] * t) for i in range(3)]
        px.append([v for _ in range(W) for v in c])

    def put(x, y, col):
        if 0 <= x < W and 0 <= y < H:
            px[y][x * 3:x * 3 + 3] = col

    for x in range(W):
        for y in (0, 1, H - 2, H - 1):
            put(x, y, (220, 220, 220))
    for y in range(H):
        for x in (0, 1, W - 2, W - 1):
            put(x, y, (220, 220, 220))

    scale = 2
    y0 = (H - len(lines) * 8 * scale) // 2
    for li, text in enumerate(lines):
        width = len(text) * 6 * scale - scale
        x0 = (W - width) // 2
        for ci, ch in enumerate(text):
            for ry, bits in enumerate(FONT[ch]):
                for rx in range(5):
                    if bits & (16 >> rx):
                        for dy in range(scale):
                            for dx in range(scale):
                                put(x0 + (ci * 6 + rx) * scale + dx,
                                    y0 + (li * 8 + ry) * scale + dy,
                                    (255, 255, 255))
    return png([bytes(r) for r in px])


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
    out = {
        "game/default_jkt_001.png": render((20, 40, 110), (5, 10, 40), ["PS2", "GAME"]),
        "installer/jkt_001.png": render((10, 90, 60), (5, 25, 20), ["UDPFS", "INST"]),
    }
    out["game/default_jkt_002.png"] = out["game/default_jkt_001.png"]
    out["installer/jkt_002.png"] = out["installer/jkt_001.png"]
    for rel, data in out.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
        print("%s %d bytes" % (rel, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
