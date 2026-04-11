"""One-shot: write nic_ble_pc/app_icon.png (32x32 RGBA, network motif). Run: python tools/gen_app_icon_png.py"""

from __future__ import annotations

import os
import struct
import zlib


def _png_rgba_32x32() -> bytes:
    w, h = 32, 32
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            dx, dy = x - 15.5, y - 15.5
            in_circle = dx * dx + dy * dy <= 14.2 * 14.2
            # RJ45 body
            rj = 10 <= x <= 22 and 12 <= y <= 22
            # wifi arcs (simple bands)
            wifi = False
            for cy, r0 in [(6, 10), (8, 7), (10, 4)]:
                d = ((x - 16) ** 2 + (y - cy) ** 2) ** 0.5
                if abs(d - r0) < 1.2 and y < 14:
                    wifi = True
            if in_circle:
                if rj:
                    r, g, b, a = 235, 240, 255, 255
                elif wifi:
                    r, g, b, a = 255, 255, 255, 255
                elif 12 <= x <= 20 and 14 <= y <= 20 and x % 3 == 0:
                    r, g, b, a = 50, 70, 130, 255
                else:
                    r, g, b, a = 41, 98, 255, 255
            else:
                r, g, b, a = 0, 0, 0, 0
            row.extend([r, g, b, a])
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    comp = zlib.compress(raw, 9)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", comp)
        + chunk(b"IEND", b"")
    )


def _png_embedded_ico(png: bytes) -> bytes:
    """Windows Vista+ ICO wrapping a single PNG image (no Pillow)."""
    offset = 6 + 16
    w, h = 32, 32
    entry = struct.pack(
        "<BBBBHHII",
        w if w < 256 else 0,
        h if h < 256 else 0,
        0,
        0,
        0,
        0,
        len(png),
        offset,
    )
    hdr = struct.pack("<HHH", 0, 1, 1)
    return hdr + entry + png


def main() -> None:
    root = os.path.dirname(os.path.abspath(__file__))
    base = os.path.normpath(os.path.join(root, "..", "nic_ble_pc"))
    data = _png_rgba_32x32()
    png_path = os.path.join(base, "app_icon.png")
    with open(png_path, "wb") as f:
        f.write(data)
    ico_path = os.path.join(base, "app_icon.ico")
    with open(ico_path, "wb") as f:
        f.write(_png_embedded_ico(data))
    print("wrote", png_path, len(data), "bytes")
    print("wrote", ico_path, os.path.getsize(ico_path), "bytes")


if __name__ == "__main__":
    main()
