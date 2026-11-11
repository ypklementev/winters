#!/usr/bin/env python3
"""Generates the MSIX logos in packaging/Assets from the same snowflake as the
app icon (tools/make_icon.py). Pure standard library.

Usage: python3 tools/make_store_assets.py
"""
from pathlib import Path

from make_icon import draw, png

OUT = Path(__file__).resolve().parent.parent / "packaging" / "Assets"


def canvas(width, height, icon):
    """Snowflake `icon` px wide, centred on a transparent width x height canvas."""
    flake = draw(icon, fit=True)
    stride = 1 + icon * 4
    ox, oy = (width - icon) // 2, (height - icon) // 2
    rows = []
    for y in range(height):
        row = bytearray(1 + width * 4)  # filter byte 0, transparent pixels
        if oy <= y < oy + icon:
            src = flake[(y - oy) * stride + 1:(y - oy + 1) * stride]
            row[1 + ox * 4:1 + (ox + icon) * 4] = src
        rows.append(bytes(row))
    return png(width, b"".join(rows), height)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        old.unlink()
    files = {}
    for scale in (100, 200):
        k = scale / 100
        s = lambda v: round(v * k)  # noqa: E731
        q = f"scale-{scale}"
        # The flake already keeps a ~6% margin, so small logos use the full square.
        files[f"Square44x44Logo.{q}.png"] = canvas(s(44), s(44), s(44))
        files[f"StoreLogo.{q}.png"] = canvas(s(50), s(50), s(50))
        # Tiles: logo about 2/3 of the tile height, as in the Windows tile templates.
        files[f"Square150x150Logo.{q}.png"] = canvas(s(150), s(150), s(100))
        files[f"Wide310x150Logo.{q}.png"] = canvas(s(310), s(150), s(100))
    for size in (16, 24, 32, 48, 256):
        img = canvas(size, size, size)  # transparent background: plated and unplated look the same
        files[f"Square44x44Logo.targetsize-{size}.png"] = img
        files[f"Square44x44Logo.targetsize-{size}_altform-unplated.png"] = img
    for name, data in sorted(files.items()):
        (OUT / name).write_bytes(data)
    print(f"wrote {len(files)} files to {OUT}")


if __name__ == "__main__":
    main()
