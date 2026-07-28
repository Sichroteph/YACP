#!/usr/bin/env python3
"""Generates the fixture PNG (pure stdlib): 64x48 8-bit grayscale with a
deterministic pattern — left half is a horizontal gradient, right half is
solid black top / solid white bottom, so scaled output rows have predictable
values at known coordinates."""
import struct
import sys
import zlib

W, H = 64, 48

def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

rows = bytearray()
for y in range(H):
    rows.append(0)  # filter: none
    for x in range(W):
        if x < W // 2:
            rows.append(int(x * 255 / (W // 2 - 1)))
        else:
            rows.append(0 if y < H // 2 else 255)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0))  # gray8
png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
png += chunk(b"IEND", b"")

with open(sys.argv[1], "wb") as f:
    f.write(png)
print(f"gen_test_image: {W}x{H} gray PNG, {len(png)} bytes")
