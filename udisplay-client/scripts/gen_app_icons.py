#!/usr/bin/env python3
"""Rasterize udisplay_icon.svg into Android mipmap icons and one desktop/QML icon.

Usage: gen_app_icons.py <icon.svg> <output_dir>

Requires rsvg-convert (Debian/Ubuntu package: librsvg2-bin) on PATH.
"""
import shutil
import subprocess
import sys
from pathlib import Path

# Standard Android launcher icon sizes, in px, per density bucket.
ANDROID_MIPMAP_SIZES = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}

# Single size shared by the desktop window icon and the in-app QML icon.
DESKTOP_ICON_SIZE = 256


def rasterize(rsvg_convert, svg_path, size, output_path):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            rsvg_convert,
            "--width", str(size),
            "--height", str(size),
            str(svg_path),
            "-o", str(output_path),
        ],
        check=True,
    )


def main():
    if len(sys.argv) != 3:
        sys.exit(f"Usage: {sys.argv[0]} <icon.svg> <output_dir>")

    svg_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])

    if not svg_path.is_file():
        sys.exit(f"gen_app_icons.py: source SVG not found: {svg_path}")

    rsvg_convert = shutil.which("rsvg-convert")
    if rsvg_convert is None:
        sys.exit(
            "gen_app_icons.py: rsvg-convert not found on PATH.\n"
            "Install it: sudo apt install librsvg2-bin (see docs/building.md)"
        )

    for density, size in ANDROID_MIPMAP_SIZES.items():
        rasterize(
            rsvg_convert, svg_path, size,
            output_dir / "android" / density / "ic_launcher.png",
        )

    rasterize(
        rsvg_convert, svg_path, DESKTOP_ICON_SIZE,
        output_dir / "icons" / "app.png",
    )


if __name__ == "__main__":
    main()
