#!/usr/bin/env python3
"""Wrap a raw 800K floppy image as a Disk Copy 4.2 (.img) file.

Mini vMac and Basilisk II accept raw disk-byte images directly. Real
System 6/7 mounters (Disk Copy 4.2, MountImage, ShrinkWrap) expect the
84-byte Disk Copy 4.2 header with a name, checksums, and a format byte
identifying the disk type. This script prepends that header.

Usage:
    raw-to-dc42.py <input.dsk> <output.img> [disk_name]

Output disk_name defaults to the input file's stem (truncated to 63
chars — DC 4.2's name field is a 64-byte Pascal string).

Header layout (big-endian, all 84 bytes go at the start of the data
fork; the raw disk data follows):

    0       1 byte    name length (Pascal string)
    1..63   63 bytes  name, padded with nulls
    64      uint32    data size in bytes (e.g. 819200 for 800K)
    68      uint32    tag size in bytes (0 for plain HFS)
    72      uint32    dataChecksum  (over the disk data)
    76      uint32    tagChecksum   (0 when tagSize == 0)
    80      byte      encoding   (0x01 = GCR 800K double-sided)
    81      byte      format     (0x22 = HFS 800K double-sided)
    82..83  bytes     magic 0x01 0x00
"""

import sys
from pathlib import Path


def dc42_checksum(data: bytes) -> int:
    """Disk Copy 4.2's 16-bit-pair sum with a 32-bit right-rotate after
    each word — applied to either disk data or tag bytes."""
    chk = 0
    for i in range(0, len(data) - 1, 2):
        word = (data[i] << 8) | data[i + 1]
        chk = (chk + word) & 0xFFFFFFFF
        chk = ((chk >> 1) | ((chk & 1) << 31)) & 0xFFFFFFFF
    return chk


def build_header(disk_name: str, data_size: int, data_checksum: int) -> bytes:
    name_bytes = disk_name.encode("mac_roman", errors="replace")[:63]
    header = bytearray(84)
    header[0] = len(name_bytes)
    header[1:1 + len(name_bytes)] = name_bytes
    # 64..67 data size
    header[64:68] = data_size.to_bytes(4, "big")
    # 68..71 tag size (always 0 for HFS-only images)
    header[68:72] = (0).to_bytes(4, "big")
    # 72..75 data checksum
    header[72:76] = data_checksum.to_bytes(4, "big")
    # 76..79 tag checksum (0)
    header[76:80] = (0).to_bytes(4, "big")
    # 80 encoding: 0x01 = GCR 800K double-sided
    header[80] = 0x01
    # 81 format: 0x22 = double-sided HFS
    header[81] = 0x22
    # 82..83 magic
    header[82:84] = b"\x01\x00"
    return bytes(header)


def main() -> int:
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    disk_name = sys.argv[3] if len(sys.argv) > 3 else src.stem

    data = src.read_bytes()
    if len(data) != 800 * 1024:
        sys.stderr.write(
            f"warning: {src.name} is {len(data)} bytes; "
            f"expected 819200 (800K). Writing anyway.\n"
        )

    checksum = dc42_checksum(data)
    header = build_header(disk_name, len(data), checksum)
    dst.write_bytes(header + data)
    print(f"  wrote {dst.name} ({len(header) + len(data)} bytes, name='{disk_name}')")
    return 0


if __name__ == "__main__":
    sys.exit(main())
