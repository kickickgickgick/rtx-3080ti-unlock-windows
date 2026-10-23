#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the reviewed V67 image for the byte-identical 595.58.03 GA102 booter."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

BOOTER_SHA256 = "056cbc064a888f80717726c54d7f64fcd68044d123870c95a74f37f4975dcaa4"
PAYLOAD_SHA256 = "2a0aa1f86d0b6afff5214d641d87d46dc98ef06ed908ff05d0ddbad5ffb7e197"
PAYLOAD_SIZE = 0xFA00
FILL_DWORD = 0x0000019C
PATCHED_WORDS = {
    0x1100: 0x00000007,
    0xF948: 0xFFFFFFFF,
    0xF950: 0x00000D44,
    0xF960: 0x00823804,
    0xF968: 0x00001FCE,
    0xF974: 0x00000000,
    0xF97C: 0x00001101,
    0xF980: 0x000084C8,
    0xF984: 0x00008E18,
    0xF98C: 0x000084C8,
    0xF990: 0x00000000,
    0xF998: 0x00001FCE,
    0xF9A4: 0x0000FFBC,
    0xF9AC: 0x00005789,
    0xF9BC: 0x00000D44,
    0xF9CC: 0x00000003,
    0xF9D4: 0x00001FCE,
    0xF9E8: 0x00000D52,
    0xF9EC: 0x000081EE,
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("booter", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if sha256(args.booter.read_bytes()) != BOOTER_SHA256:
        raise SystemExit("refusing V67 generation: GA102 booter hash is not pinned")
    payload = bytearray(FILL_DWORD.to_bytes(4, "little") * (PAYLOAD_SIZE // 4))
    for offset, value in PATCHED_WORDS.items():
        payload[offset:offset + 4] = value.to_bytes(4, "little")
    data = bytes(payload)
    if sha256(data) != PAYLOAD_SHA256:
        raise SystemExit("generated V67 payload differs from reviewed reference")
    args.output.write_bytes(data)
    print(f"{args.output.name}\t{len(data)}\t{sha256(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
