#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Extract GA102 boot assets from NVIDIA's pinned 595.58.03 source archive.

The bundle format and defensive parser are adapted from Zagitta/gpu-unlock's
firmware/extract_boot_assets.py.  This script intentionally does not create a
V67 payload: no reviewed 595.58.03 ROP payload is currently pinned.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x314C5442
PINNED = {
    "g_bindata_kgspGetBinArchiveBooterLoadUcode_GA102.c":
        "8367593573e118667f4faf4e7ffd876aa9652c41146befd20ac463cf8c9b5cab",
    "g_bindata_kgspGetBinArchiveGspRmBoot_GA102.c":
        "64f9296d4a2a57323ffa57cb04e499fdea077471bdbf35a0b01e8439e8a5a3f6",
}


def array_bytes(text: str, label: str) -> bytes:
    marker = f"BINDATA_LABEL_{label}_data[]"
    if text.count(marker) != 1:
        raise ValueError(f"missing or duplicate {label}")
    start = text.index(marker)
    window = text[max(0, start - 400):start]
    compression = re.findall(r"COMPRESSION: (YES|NO)", window)
    sizes = re.findall(r"DATA SIZE \(bytes\): (\d+)", window)
    if not compression or not sizes:
        raise ValueError(f"missing storage declaration for {label}")
    expected = int(sizes[-1])
    if not 0 < expected <= 16 * 1024 * 1024:
        raise ValueError(f"invalid {label} size")
    declaration = re.match(
        re.escape(marker) + r"\s*=\s*\{([^}]*)\};", text[start:], re.S
    )
    if not declaration:
        raise ValueError(f"malformed {label} declaration")
    tokens = declaration[1].strip().rstrip(",").split(",")
    if any(not re.fullmatch(r"\s*0x[0-9a-fA-F]{2}\s*", token) for token in tokens):
        raise ValueError(f"non-byte token in {label}")
    raw = bytes(int(token, 16) for token in tokens)
    if compression[-1] == "YES":
        stored = re.findall(r"COMPRESSED SIZE \(bytes\): (\d+)", window)
        if not stored or len(raw) != int(stored[-1]):
            raise ValueError(f"compressed {label} size mismatch")
        inflater = zlib.decompressobj(-15)
        data = inflater.decompress(raw, expected + 1)
        if not inflater.eof or len(inflater.unused_data) != 8 or inflater.unconsumed_tail:
            raise ValueError(f"invalid compressed stream for {label}")
        crc, size = struct.unpack("<II", inflater.unused_data)
        if size != expected or crc != zlib.crc32(data):
            raise ValueError(f"checksum mismatch for {label}")
    else:
        data = raw
    if len(data) != expected:
        raise ValueError(f"inflated {label} length mismatch")
    return data


def build_bundle(text: str) -> bytes:
    image = array_bytes(text, "IMAGE_PROD")
    header = array_bytes(text, "HEADER_PROD")
    signatures = array_bytes(text, "SIG_PROD")
    patch_loc = array_bytes(text, "PATCH_LOC")
    patch_meta = array_bytes(text, "PATCH_META")
    num_sigs = array_bytes(text, "NUM_SIGS")
    if len(header) != 36 or len(patch_loc) != 4 or len(patch_meta) != 12 or len(num_sigs) != 4:
        raise ValueError("unexpected BooterLoad record size")
    fields = struct.unpack("<9I", header)
    loc = struct.unpack("<I", patch_loc)[0]
    _fuse, engine, ucode = struct.unpack("<3I", patch_meta)
    count = struct.unpack("<I", num_sigs)[0]
    if not count or len(signatures) != count * 384 or fields[4] != 1 or not ucode:
        raise ValueError("unsupported production Booter signature/application table")
    if loc < fields[2] or loc + 384 > fields[2] + fields[3] or fields[2] + fields[3] > len(image):
        raise ValueError("signature patch is outside DMEM image")
    for offset, size in ((fields[0], fields[1]), (fields[2], fields[3]), (fields[5], fields[6])):
        if offset + size > len(image) or offset % 256 or size % 256:
            raise ValueError("invalid Falcon transfer range")
    packed = struct.pack(
        "<14I", fields[0], fields[1], fields[2], fields[3], fields[4],
        fields[5], fields[6], fields[7], fields[8], loc, ucode, engine,
        count, len(signatures),
    )
    return struct.pack("<II", MAGIC, 1) + packed + struct.pack(
        "<II", len(image), len(signatures)
    ) + image + signatures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generated = args.source_root / "src" / "nvidia" / "generated"
    texts: dict[str, str] = {}
    for name, digest in PINNED.items():
        data = (generated / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"pinned generated source mismatch: {name}")
        texts[name] = data.decode("ascii")
    booter = build_bundle(texts["g_bindata_kgspGetBinArchiveBooterLoadUcode_GA102.c"])
    boot_text = texts["g_bindata_kgspGetBinArchiveGspRmBoot_GA102.c"]
    image = array_bytes(boot_text, "UCODE_IMAGE_PROD")
    desc = array_bytes(boot_text, "UCODE_DESC_PROD")
    if len(desc) != 84:
        raise ValueError("unexpected bootloader descriptor length")
    fields = struct.unpack("<21I", desc)
    if fields[0] != 5 or fields[14] != 1 or fields[19] != len(image) or fields[20] != 0:
        raise ValueError("unsupported bootloader descriptor")
    for index in (1, 3, 8, 10, 12):
        if not fields[index + 1] or fields[index] + fields[index + 1] > len(image):
            raise ValueError("bootloader descriptor range outside image")
    assets = {
        "booter_load_ga102.bin": booter,
        "gsp_boot_ga102.bin": image,
        "gsp_boot_desc_ga102.bin": desc,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in assets.items():
        (args.output / name).write_bytes(data)
        print(f"{name}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, zlib.error) as error:
        sys.exit(f"extract failed: {error}")
