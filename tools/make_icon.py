#!/usr/bin/env python3
"""Generates src/app/snowflake.ico (PNG-compressed entries, Vista+).

Same drawing as CreateSnowflakeIcon() in src/app/icon.cpp: white snowflake
with a blue outline. Pure standard library. Usage: python3 tools/make_icon.py
"""
import math
import struct
import sys
import zlib
from pathlib import Path


def draw(size):
    c = size * 0.5
    arm = size * 0.44
    half = max(0.9, size / 18.0)
    outline = max(1.0, size / 22.0)
    segs = []
    for k in range(6):
        a = k * math.pi / 3 + math.pi / 2
        dx, dy = math.cos(a), math.sin(a)
        segs.append((c, c, c + dx * arm, c + dy * arm))
        bx, by = c + dx * arm * 0.55, c + dy * arm * 0.55
        for side in (-1, 1):
            ba = a + side * 0.75
            segs.append((bx, by, bx + math.cos(ba) * arm * 0.32, by + math.sin(ba) * arm * 0.32))

    def dist(px, py, s):
        ax, ay, bx, by = s
        dx, dy = bx - ax, by - ay
        l2 = dx * dx + dy * dy
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2)) if l2 else 0.0
        return math.hypot(ax + t * dx - px, ay + t * dy - py)

    rows = []
    for y in range(size):
        row = bytearray([0])  # PNG filter: none
        for x in range(size):
            d = min(dist(x + 0.5, y + 0.5, s) for s in segs)
            fill = max(0.0, min(1.0, half + 0.5 - d))
            edge = max(0.0, min(1.0, half + outline + 0.5 - d))
            r = 0x2F + (255 - 0x2F) * fill
            g = 0x6F + (255 - 0x6F) * fill
            b = 0xC4 + (255 - 0xC4) * fill
            row += bytes([int(r + 0.5), int(g + 0.5), int(b + 0.5), int(edge * 255 + 0.5)])
        rows.append(bytes(row))
    return b"".join(rows)


def png(size, raw):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    images = [png(s, draw(s)) for s in sizes]
    out = bytearray(struct.pack("<HHH", 0, 1, len(sizes)))
    offset = 6 + 16 * len(sizes)
    for s, img in zip(sizes, images):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    for img in images:
        out += img
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "src/app/snowflake.ico"
    target.write_bytes(out)
    print(f"wrote {target} ({len(out)} bytes)")


if __name__ == "__main__":
    main()
