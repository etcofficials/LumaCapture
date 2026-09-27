"""Generates resources/lumacapture.ico (PNG-compressed ICO entries) without any
image library: the icon is drawn procedurally with 4x4 supersampling.
Design matches Icons.cpp IconId::App: blue rounded square, white ring, red dot.

Usage: python scripts/make-icon.py
"""
import pathlib
import struct
import zlib

BLUE = (0x3B, 0x82, 0xF6)
WHITE = (0xFF, 0xFF, 0xFF)
RED = (0xE5, 0x48, 0x4D)


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def sample(u, v):
    """Colour (r,g,b,a) at design coordinates in 0..24."""
    d2 = (u - 12) ** 2 + (v - 12) ** 2
    if d2 <= 3 ** 2:
        return RED + (255,)
    if d2 <= 6 ** 2:
        return WHITE + (255,)
    if inside_round_rect(u, v, 1, 1, 23, 23, 6):
        return BLUE + (255,)
    return (0, 0, 0, 0)


def render(size):
    ss = 4
    rows = []
    for y in range(size):
        row = bytearray([0])  # PNG filter type 0
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (x + (sx + 0.5) / ss) * 24 / size
                    v = (y + (sy + 0.5) / ss) * 24 / size
                    r, g, b, a = sample(u, v)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = ss * ss
            a = acc[3] / n
            if a > 0:
                row += bytes([round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(a)])
            else:
                row += bytes([0, 0, 0, 0])
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [render(s) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, png in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    out = pathlib.Path(__file__).resolve().parent.parent / "resources" / "lumacapture.ico"
    out.write_bytes(header + entries + b"".join(images))
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
