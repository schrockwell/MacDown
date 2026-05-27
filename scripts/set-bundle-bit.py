#!/usr/bin/env python3
"""Set the 'has bundle' Finder bit on Retro68 build outputs.

Retro68's add_application sets type and creator on the produced
MacBinary file but leaves Finder flags at zero. Without the 'has bundle'
bit (0x2000), System 6/7's Finder won't read the file's BNDL/ICN#
resources during a desktop rebuild — so the app shows up with the
generic application icon instead of the one defined in the .r file.

Mini vMac tends to be lenient and display the icon anyway, but real
hardware (and many emulator setups with a populated desktop database)
require the bit to be set.

Handles two output formats:

  * MacBinary  — single-file container, header offset 73 holds the
    high byte of Finder flags. CRC at offsets 124-125 is recomputed
    over bytes 0-123 (MacBinary II CRC-CCITT, init 0, poly 0x1021)
    so both MB I and MB II decoders accept the patched file.

  * AppleDouble — sidecar layout (e.g. `%MacDown.ad`) with a header
    pointing to a Finder Info entry (ID 9). The first byte of the
    flags inside the FInfo struct gets the bundle bit set.
"""

import struct
import sys
from pathlib import Path

BUNDLE_BIT = 0x20   # high byte of kHasBundle (0x2000)


def crc16_ccitt(data: bytes) -> int:
    """MacBinary II CRC: init 0, poly 0x1021, no final XOR."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def patch_macbinary(path: Path) -> str:
    data = bytearray(path.read_bytes())
    if len(data) < 128:
        return "skipped: too small to be MacBinary"
    if data[0] != 0 or data[74] != 0:
        return "skipped: header byte 0 / 74 not zero (not MacBinary)"

    # Set 'has bundle' bit (high byte of Finder flags) and recompute
    # the CRC at offsets 124-125 so decoders that verify it accept
    # the modified file. We deliberately do NOT touch the version
    # bytes (122-123): Basilisk II's ExtFS only accepts the MacBinary I
    # form Retro68 emits and rejects an explicit MB II marker; many
    # other decoders sniff the file's MB version from the CRC field's
    # value rather than from the version bytes, so a correct CRC keeps
    # everyone happy.
    if data[73] & BUNDLE_BIT:
        return "already set"
    data[73] |= BUNDLE_BIT
    crc = crc16_ccitt(bytes(data[:124]))
    data[124] = (crc >> 8) & 0xFF
    data[125] = crc & 0xFF
    path.write_bytes(bytes(data))
    return "patched"


def patch_appledouble(path: Path) -> str:
    data = bytearray(path.read_bytes())
    if len(data) < 26:
        return "skipped: too small"
    magic, version = struct.unpack(">II", bytes(data[0:8]))
    if magic not in (0x00051600, 0x00051607):
        return "skipped: not AppleDouble/AppleSingle"
    num_entries = struct.unpack(">H", bytes(data[24:26]))[0]
    for i in range(num_entries):
        off = 26 + i * 12
        entry_id, entry_off, entry_len = struct.unpack(">III", bytes(data[off:off + 12]))
        if entry_id == 9 and entry_len >= 10:
            # FInfo layout: type(4) creator(4) flags(2 BE) ...
            flag_high = entry_off + 8
            if data[flag_high] & BUNDLE_BIT:
                return "already set"
            data[flag_high] |= BUNDLE_BIT
            path.write_bytes(bytes(data))
            return "patched"
    return "skipped: no Finder Info entry"


def detect_and_patch(path: Path) -> str:
    if not path.exists() or path.stat().st_size == 0:
        return "skipped: empty"
    head = path.read_bytes()[:8]
    if len(head) >= 8 and head[0:4] in (b"\x00\x05\x16\x00", b"\x00\x05\x16\x07"):
        return patch_appledouble(path)
    return patch_macbinary(path)


def main() -> int:
    if len(sys.argv) < 2:
        sys.stderr.write(f"usage: {sys.argv[0]} <file>...\n")
        return 2
    for arg in sys.argv[1:]:
        p = Path(arg)
        result = detect_and_patch(p)
        print(f"  {p.name}: {result}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
