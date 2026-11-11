#!/usr/bin/env python3
"""Generates packaging/macos/WinTers.icns (macOS only: needs iconutil).

Same snowflake as tools/make_icon.py. Usage: python3 tools/make_icns.py
"""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_icon import draw, png  # noqa: E402


def main():
    root = Path(__file__).resolve().parent.parent
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "packaging/macos/WinTers.icns"
    cache = {}
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "WinTers.iconset"
        iconset.mkdir()
        for base in (16, 32, 128, 256, 512):
            for mult in (1, 2):
                size = base * mult
                if size not in cache:
                    print(f"drawing {size}px", flush=True)
                    cache[size] = png(size, draw(size, fit=True))
                suffix = "@2x" if mult == 2 else ""
                (iconset / f"icon_{base}x{base}{suffix}.png").write_bytes(cache[size])
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(target)], check=True)
    print(f"wrote {target} ({target.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
