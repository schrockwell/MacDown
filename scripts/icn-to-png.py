#!/usr/bin/env python3
"""Extract 'ICN#' resources from a Rez source file back to 32x32 PNGs.

Usage:
    scripts/icn-to-png.py <input.r> <resource-id> <out.png> [<resource-id> <out.png> ...]

An 'ICN#' resource is a pair of 128-byte 1-bit bitmaps (data + mask),
each 32 rows of 32 bits. We emit the DATA layer as a grayscale PNG
(black where data bit = 1, white elsewhere). The mask is dropped on
extraction — png-to-icn.py rebuilds it via flood-fill, so the round-
trip preserves the editable artwork without depending on the mask.
"""

import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("error: Pillow not installed. Try: pip3 install Pillow\n")
    sys.exit(1)


def find_icn_block(text: str, rid: int) -> str:
    """Return everything between `resource 'ICN#' (<rid>, ...) {` and its
    matching `};`. Falls back to a broad search if the comment-stripped
    block isn't found."""
    header = re.search(
        rf"resource\s+'ICN#'\s*\(\s*{rid}\b[^)]*\)\s*\{{",
        text,
    )
    if not header:
        sys.stderr.write(f"error: ICN# ({rid}) not found in input\n")
        sys.exit(1)
    start = header.end()
    depth = 1
    i = start
    while i < len(text) and depth > 0:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return text[start:i - 1]


def extract_hex(block: str) -> bytes:
    """Pull every $"…" hex literal in order and concatenate the bytes.
    Ignores comments and whitespace between literals."""
    no_comments = re.sub(r"/\*.*?\*/", "", block, flags=re.DOTALL)
    literals = re.findall(r'\$"([0-9A-Fa-f\s]+)"', no_comments)
    hex_str = "".join(re.sub(r"\s+", "", lit) for lit in literals)
    if len(hex_str) % 2:
        sys.stderr.write("error: odd-length hex stream\n")
        sys.exit(1)
    return bytes.fromhex(hex_str)


def data_to_image(data: bytes) -> Image.Image:
    """First 128 bytes = data layer, 32 rows of 4 bytes (MSB-first)."""
    if len(data) < 128:
        sys.stderr.write(f"error: ICN# data layer too short ({len(data)} < 128)\n")
        sys.exit(1)
    img = Image.new("L", (32, 32), 255)
    px = img.load()
    for y in range(32):
        for bx in range(4):
            byte = data[y * 4 + bx]
            for bit in range(8):
                if byte & (1 << (7 - bit)):
                    px[bx * 8 + bit, y] = 0
    return img


def main() -> int:
    if len(sys.argv) < 4 or (len(sys.argv) - 2) % 2 != 0:
        sys.stderr.write(__doc__)
        return 2
    in_path = Path(sys.argv[1])
    text = in_path.read_text()
    pairs = sys.argv[2:]
    for rid_str, out_str in zip(pairs[0::2], pairs[1::2]):
        rid = int(rid_str)
        out_path = Path(out_str)
        block = find_icn_block(text, rid)
        data = extract_hex(block)
        img = data_to_image(data)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        img.save(out_path)
        print(f"wrote {out_path} (ICN# {rid})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
