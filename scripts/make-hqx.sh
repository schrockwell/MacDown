#!/bin/bash
# Produce a BinHex 4.0 (.hqx) file from MdEdit.bin.
#
# BinHex is text-safe and self-contained: it embeds the file name,
# type, creator, Finder flags, and both forks into a 7-bit ASCII
# stream. StuffIt Expander on every classic Mac handles it without
# any preliminary type/creator setup — which makes it the most
# bulletproof way to send a Mac app over a route that strips
# metadata (FTP, plain HTTP, FAT-formatted media, etc.).
#
# Pipeline:
#   1. macbinary decode    .bin   →   real two-fork Mac file on the
#                                     macOS extended-attribute layer
#   2. binhex   encode     file   →   .hqx
#
# Requires Apple's binhex/macbinary tools (shipped with macOS).
#
# Usage: ./scripts/make-hqx.sh <input.bin> <output.hqx>

set -e

if [ $# -ne 2 ]; then
    echo "usage: $0 <input.bin> <output.hqx>" >&2
    exit 2
fi

SRC="$1"
DST="$2"

for tool in /usr/bin/macbinary /usr/bin/binhex; do
    if [ ! -x "$tool" ]; then
        echo "error: $tool not found — needs Apple's binhex/macbinary tools (macOS)" >&2
        exit 1
    fi
done

# Stage the decoded file in a fresh temp dir so its name/forks survive
# even if a previous run left junk behind.
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/mdedit-hqx-XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

/usr/bin/macbinary decode -C "$STAGE" "$SRC" >/dev/null 2>&1
DECODED="$STAGE/$(ls "$STAGE")"

/usr/bin/binhex encode -n -o "$DST" "$DECODED"
echo "  wrote $(basename "$DST") ($(wc -c < "$DST" | tr -d ' ') bytes)"
