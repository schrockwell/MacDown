#!/usr/bin/env python3
"""Convert a MacPaint icon canvas into a Rez ICN# resource block.

Usage:
    scripts/macpaint-to-icn.py <file.mac> [resource_id] [label]

Reads a MacPaint document, decompresses the PackBits-encoded 576x720
1-bit bitmap, and extracts one 32x32 icon from the upper-left corner
(canvas cols 0..31, rows 0..31). Emits a single `resource 'ICN#'`
definition on stdout — drop into resources/MacDown.r in place of the
existing ICN# of the same ID.

Defaults: resource_id = 128 (app icon), label = "App".
For the document icon: pass 129 and "Document".

MacPaint file layout:

    0..511    header (mostly unused; pattern palette + version)
    512..end  720 scanlines, each independently PackBits-compressed
              to 72 bytes raw (576 pixels = 576 bits = 72 bytes)

PackBits decode loop (per scanline):

    flag = next byte
    if  0x00 <= flag <= 0x7F:  copy flag+1 verbatim bytes
    if  0x81 <= flag <= 0xFF:  repeat next byte (257 - flag) times
    if         flag == 0x80:   no-op (rare)

Bits in each byte are MSB-first — pixel 0 of a byte is the high bit.
MacPaint pixels: 1 = drawn (black), 0 = paper (white). That matches
ICN# data bits directly, so no inversion is needed. The mask is
computed by flood-filling the white pixels reachable from any of the
four corners — everything not reached is part of the icon silhouette,
which keeps the interior of an outlined shape opaque.
"""

import sys
from collections import deque
from pathlib import Path

CANVAS_W = 576
CANVAS_H = 720
SCANLINE_BYTES = CANVAS_W // 8   # 72 bytes per row
HEADER_BYTES = 512


def packbits_decode_scanline(data: bytes, offset: int, target_bytes: int):
    """Decode one PackBits scanline into exactly target_bytes bytes.
    Returns (decoded, new_offset)."""
    out = bytearray()
    pos = offset
    while len(out) < target_bytes:
        if pos >= len(data):
            raise ValueError(f"unexpected EOF at offset {pos} "
                             f"(need {target_bytes - len(out)} more bytes)")
        n = data[pos]
        pos += 1
        if n <= 0x7F:
            count = n + 1
            out.extend(data[pos:pos + count])
            pos += count
        elif n == 0x80:
            pass
        else:
            count = 257 - n
            if pos >= len(data):
                raise ValueError("unexpected EOF in run-length byte")
            out.extend(bytes([data[pos]] * count))
            pos += 1
    if len(out) > target_bytes:
        raise ValueError(f"scanline overshot by {len(out) - target_bytes} bytes")
    return bytes(out), pos


def decompress_macpaint(data: bytes):
    """Return a 720-row × 576-col grid of 0/1 ints."""
    pos = HEADER_BYTES
    rows = []
    for y in range(CANVAS_H):
        scanline, pos = packbits_decode_scanline(data, pos, SCANLINE_BYTES)
        row = []
        for byte in scanline:
            for bit in range(7, -1, -1):
                row.append((byte >> bit) & 1)
        rows.append(row)
    return rows


def extract_32(canvas, origin_x: int, origin_y: int):
    """Slice a 32x32 region out of the canvas grid."""
    if origin_x + 32 > CANVAS_W or origin_y + 32 > CANVAS_H:
        raise ValueError("icon region falls off the canvas")
    return [canvas[origin_y + dy][origin_x:origin_x + 32] for dy in range(32)]


def compute_mask_32(bits):
    """Flood-fill white pixels from the four corners; the complement
    is the icon's silhouette (interior of outlined shapes stays opaque)."""
    outside = [[False] * 32 for _ in range(32)]
    queue = deque()
    for cx, cy in [(0, 0), (31, 0), (0, 31), (31, 31)]:
        if bits[cy][cx] == 0 and not outside[cy][cx]:
            outside[cy][cx] = True
            queue.append((cx, cy))
    while queue:
        x, y = queue.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < 32 and 0 <= ny < 32 and not outside[ny][nx]:
                if bits[ny][nx] == 0:
                    outside[ny][nx] = True
                    queue.append((nx, ny))
    return [[not outside[y][x] for x in range(32)] for y in range(32)]


def rasterize_rows(predicate):
    """8 hex string literals, 4 rows of 32 bits each."""
    rows = []
    for y in range(32):
        word = 0
        for x in range(32):
            if predicate(x, y):
                word |= 1 << (31 - x)
        rows.append(f"{word:08X}")
    return ['        $"' + " ".join(rows[i:i + 4]) + '"' for i in range(0, 32, 4)]


def icn_block(bits, resource_id: int, label: str) -> str:
    mask = compute_mask_32(bits)
    data_lines = rasterize_rows(lambda x, y: bits[y][x] == 1)
    mask_lines = rasterize_rows(lambda x, y: mask[y][x])
    # Rez: data and mask are two array entries separated by ONE comma;
    # the string literals inside each block are concatenated, no commas.
    data_lines[-1] = data_lines[-1] + ","
    out = [f"resource 'ICN#' ({resource_id}, \"{label}\") {{",
           "    {",
           "        /* data */"]
    out.extend(data_lines)
    out.append("        /* mask */")
    out.extend(mask_lines)
    out.append("    }")
    out.append("};")
    return "\n".join(out)


def main() -> int:
    if not (2 <= len(sys.argv) <= 4):
        sys.stderr.write(__doc__)
        return 2
    path = Path(sys.argv[1])
    resource_id = int(sys.argv[2]) if len(sys.argv) >= 3 else 128
    label       = sys.argv[3]      if len(sys.argv) >= 4 else "App"

    data = path.read_bytes()
    if len(data) < HEADER_BYTES + SCANLINE_BYTES:
        sys.stderr.write(f"error: {path} is too small to be a MacPaint file\n")
        return 1

    canvas = decompress_macpaint(data)
    bits = extract_32(canvas, 0, 0)
    print(icn_block(bits, resource_id, label))
    return 0


if __name__ == "__main__":
    sys.exit(main())
