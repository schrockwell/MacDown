#!/usr/bin/env python3
"""Convert a 32x32 PNG into a Rez ICN# resource block.

Usage:
    scripts/png-to-icn.py <app.png> <document.png>

Emits two `resource 'ICN#'` definitions on stdout (128 for the app icon,
129 for the document icon), ready to paste into resources/MdEdit.r.

Each ICN# resource is 256 bytes: 128 bytes of 1-bit image data followed
by 128 bytes of mask. Within Rez, both blocks are written as a sequence
of concatenated hex string literals separated by ONE comma between data
and mask — no commas inside either block.

  - **Data** bit = 1 wherever the pixel is dark (grayscale < 128). These
    pixels paint black on-screen.
  - **Mask** bit = 1 wherever the pixel belongs to the icon's silhouette.
    Computed by flood-filling "outside" from the four corners across all
    pure-white pixels; everything not reached is treated as inside the
    icon. The result is a filled silhouette (white interiors of an
    outlined shape stay opaque), without requiring the source PNG to
    have an alpha channel.
"""

import sys
from collections import deque
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("error: Pillow not installed. Try: pip3 install Pillow\n")
    sys.exit(1)


def compute_mask(px, w: int = 32, h: int = 32) -> list[list[bool]]:
    """Flood-fill the background (pure-white, reachable from any corner)
    and return a 32x32 grid where True = inside the icon silhouette."""
    outside = [[False] * w for _ in range(h)]
    queue = deque()
    for corner in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        cx, cy = corner
        if px[cx, cy] >= 254 and not outside[cy][cx]:
            outside[cy][cx] = True
            queue.append(corner)
    while queue:
        x, y = queue.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not outside[ny][nx]:
                if px[nx, ny] >= 254:
                    outside[ny][nx] = True
                    queue.append((nx, ny))
    return [[not outside[y][x] for x in range(w)] for y in range(h)]


def rasterize_rows(predicate) -> list[str]:
    """Return 8 hex string literals, 4 rows of 32 bits each."""
    rows = []
    for y in range(32):
        bits = 0
        for x in range(32):
            if predicate(x, y):
                bits |= 1 << (31 - x)
        rows.append(f"{bits:08X}")
    return ['        $"' + " ".join(rows[i:i + 4]) + '"' for i in range(0, 32, 4)]


def png_to_icn_block(path: Path, resource_id: int, label: str) -> str:
    img = Image.open(path).convert("L")
    if img.size != (32, 32):
        sys.stderr.write(f"error: {path}: expected 32x32, got {img.size[0]}x{img.size[1]}\n")
        sys.exit(1)
    px = img.load()
    mask_grid = compute_mask(px)

    data_lines = rasterize_rows(lambda x, y: px[x, y] < 128)
    mask_lines = rasterize_rows(lambda x, y: mask_grid[y][x])

    # Rez: data + mask are two array entries separated by a comma. Each
    # entry is itself a sequence of string literals that the assembler
    # concatenates — no commas inside.
    data_lines[-1] = data_lines[-1] + ","
    out = [f"resource 'ICN#' ({resource_id}, \"{label}\") {{", "    {", "        /* data */"]
    out.extend(data_lines)
    out.append("        /* mask */")
    out.extend(mask_lines)
    out.append("    }")
    out.append("};")
    return "\n".join(out)


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    print(png_to_icn_block(Path(sys.argv[1]), 128, "App"))
    print()
    print(png_to_icn_block(Path(sys.argv[2]), 129, "Document"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
