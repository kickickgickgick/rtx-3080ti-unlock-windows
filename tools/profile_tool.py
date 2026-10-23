#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create and compile a fail-closed RTX 3080 Ti machine profile."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

FORMAT = "rtx3080ti-probe-v1"
POLICY = "ga102-rtx3080ti-v0.14"
SUPPORTED_IDENTITY = 0x220810DE
SUPPORTED_CHIP = 0xB72000A1
SUPPORTED_FB_MB = 0x3000
SUPPORTED_BAR_SIZE = 0x01000000
SUPPORTED_PROM_SIZE = 0x00100000
CFG_KEYS = {
    "format", "identity", "class_revision", "subsystem", "segment", "bus",
    "device", "function", "root_hid", "root_uid", "root_port_device", "root_port_function",
    "bar0_base", "bar0_maximum", "bar0_size", "bar0_translation",
    "bar0_granularity", "chip_id", "fb_mb", "cold_ss0", "cold_ss1",
    "prom_size", "gpu_writes",
}
NUMERIC_KEYS = CFG_KEYS - {"format"}


def parse_cfg(path: Path) -> dict[str, int | str]:
    raw = path.read_bytes()
    if len(raw) > 16384 or b"\0" in raw:
        raise ValueError("probe profile is too large or contains NUL")
    text = raw.decode("ascii")
    values: dict[str, int | str] = {}
    for number, source_line in enumerate(text.splitlines(), 1):
        line = source_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.count("=") != 1:
            raise ValueError(f"line {number}: expected key=value")
        key, value = line.split("=", 1)
        if key not in CFG_KEYS or key in values:
            raise ValueError(f"line {number}: unknown or duplicate key {key!r}")
        if key == "format":
            if value != FORMAT:
                raise ValueError("unsupported probe profile format")
            values[key] = value
        else:
            if not re.fullmatch(r"0x[0-9a-f]{16}", value):
                raise ValueError(f"line {number}: non-canonical hexadecimal value")
            values[key] = int(value, 16)
    missing = CFG_KEYS - values.keys()
    if missing:
        raise ValueError("missing probe fields: " + ", ".join(sorted(missing)))
    return values


def cold_selectors_valid(ss0: int, ss1: int) -> bool:
    return ss1 == 7 and ss0 not in (0xFFFFFFFF, 0x88888888) and all(
        ((ss0 >> (index * 4)) & 0xF) < 8 for index in range(8)
    )


def validate_probe(values: dict[str, int | str]) -> None:
    integer = {key: int(values[key]) for key in NUMERIC_KEYS}
    for key in ("identity", "class_revision", "subsystem", "root_hid", "root_uid",
                "chip_id", "fb_mb",
                "cold_ss0", "cold_ss1"):
        if integer[key] > 0xFFFFFFFF:
            raise ValueError(f"{key} is outside the 32-bit probe field")
    if integer["identity"] != SUPPORTED_IDENTITY:
        raise ValueError("probe is not an RTX 3080 Ti 10de:2208")
    if integer["root_hid"] not in (0x0A0341D0, 0x0A0841D0):
        raise ValueError("unsupported PCI root HID")
    if integer["class_revision"] >> 24 != 3:
        raise ValueError("target is not a display controller")
    if integer["subsystem"] in (0, 0xFFFFFFFF):
        raise ValueError("invalid subsystem identity")
    if integer["chip_id"] != SUPPORTED_CHIP or integer["fb_mb"] != SUPPORTED_FB_MB:
        raise ValueError("chip or framebuffer size is outside the validated GA102 policy")
    if integer["bar0_size"] != SUPPORTED_BAR_SIZE or integer["bar0_translation"] != 0:
        raise ValueError("BAR0 layout is outside the validated 16 MiB policy")
    if integer["bar0_granularity"] != 32:
        raise ValueError("BAR0 is not a 32-bit MMIO resource")
    base, maximum = integer["bar0_base"], integer["bar0_maximum"]
    if base > 0xFFFFFFFF or base % SUPPORTED_BAR_SIZE:
        raise ValueError("BAR0 base is not a supported aligned 32-bit address")
    if maximum not in (base + SUPPORTED_BAR_SIZE - 1, SUPPORTED_BAR_SIZE - 1):
        raise ValueError("BAR0 descriptor maximum does not match its base and size")
    if integer["prom_size"] != SUPPORTED_PROM_SIZE or integer["gpu_writes"] != 0:
        raise ValueError("probe did not attest a read-only 1 MiB PROM capture")
    limits = {
        "segment": 0xFFFF, "bus": 0xFF, "device": 0x1F, "function": 7,
        "root_port_device": 0x1F, "root_port_function": 7,
    }
    for key, maximum_value in limits.items():
        if integer[key] > maximum_value:
            raise ValueError(f"{key} is outside PCI bounds")
    if not cold_selectors_valid(integer["cold_ss0"], integer["cold_ss1"]):
        raise ValueError("cold selector state is outside the validated policy")


def create_profile(cfg_path: Path, prom_path: Path, output: Path) -> None:
    values = parse_cfg(cfg_path)
    validate_probe(values)
    prom = prom_path.read_bytes()
    if len(prom) != SUPPORTED_PROM_SIZE or prom[:2] != b"\x55\xaa":
        raise ValueError("PROM must be an exact 1 MiB image beginning with 55aa")
    number = {key: int(values[key]) for key in NUMERIC_KEYS}
    profile = {
        "schema": 1,
        "policy": POLICY,
        "pci": {
            "identity": number["identity"], "subsystem": number["subsystem"],
            "class_revision": number["class_revision"], "segment": number["segment"],
            "bus": number["bus"], "device": number["device"],
            "function": number["function"],
            "root_hid": number["root_hid"], "root_uid": number["root_uid"],
            "root_port_device": number["root_port_device"],
            "root_port_function": number["root_port_function"],
        },
        "bar0": {"base": number["bar0_base"], "size": number["bar0_size"]},
        "gpu": {"chip_id": number["chip_id"], "fb_mb": number["fb_mb"]},
        "cold_sample": {"ss0": number["cold_ss0"], "ss1": number["cold_ss1"]},
        "firmware": {"prom_size": len(prom), "prom_sha256": hashlib.sha256(prom).hexdigest()},
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes((json.dumps(profile, indent=2, sort_keys=True) + "\n").encode("utf-8"))


def load_profile(path: Path) -> dict:
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON field {key!r}")
            result[key] = value
        return result

    profile = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    if set(profile) != {"schema", "policy", "pci", "bar0", "gpu", "cold_sample", "firmware"}:
        raise ValueError("profile has missing or unknown top-level fields")
    if (not isinstance(profile["schema"], int) or isinstance(profile["schema"], bool) or
            profile["schema"] != 1 or profile["policy"] != POLICY):
        raise ValueError("unsupported machine profile schema or policy")
    expected = {
        "pci": {"identity", "subsystem", "class_revision", "segment", "bus", "device",
                "function", "root_hid", "root_uid", "root_port_device", "root_port_function"},
        "bar0": {"base", "size"}, "gpu": {"chip_id", "fb_mb"},
        "cold_sample": {"ss0", "ss1"}, "firmware": {"prom_size", "prom_sha256"},
    }
    for section, keys in expected.items():
        if not isinstance(profile[section], dict) or set(profile[section]) != keys:
            raise ValueError(f"profile section {section!r} has missing or unknown fields")
    for section in ("pci", "bar0", "gpu", "cold_sample"):
        for key, value in profile[section].items():
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                raise ValueError(f"profile field {section}.{key} must be a non-negative integer")
    if (not isinstance(profile["firmware"]["prom_size"], int) or
            isinstance(profile["firmware"]["prom_size"], bool) or
            profile["firmware"]["prom_size"] < 0):
        raise ValueError("profile field firmware.prom_size must be a non-negative integer")
    flat = {
        "format": FORMAT, **profile["pci"], "bar0_base": profile["bar0"]["base"],
        "bar0_maximum": profile["bar0"]["base"] + profile["bar0"]["size"] - 1,
        "bar0_size": profile["bar0"]["size"], "bar0_translation": 0,
        "bar0_granularity": 32, **profile["gpu"],
        "cold_ss0": profile["cold_sample"]["ss0"],
        "cold_ss1": profile["cold_sample"]["ss1"],
        "prom_size": profile["firmware"]["prom_size"], "gpu_writes": 0,
    }
    validate_probe(flat)
    digest = profile["firmware"]["prom_sha256"]
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("invalid PROM SHA-256")
    return profile


def emit_header(profile_path: Path, output: Path) -> None:
    profile = load_profile(profile_path)
    pci, bar, gpu, firmware = profile["pci"], profile["bar0"], profile["gpu"], profile["firmware"]
    lines = [
        "/* Generated locally by tools/profile_tool.py; do not publish. */",
        "#pragma once",
        f"#define GU_PROFILE_SCHEMA {profile['schema']}U",
        f"#define GU_BOARD_ID 0x{pci['identity']:08x}U",
        f"#define GU_BOARD_SUBSYSTEM 0x{pci['subsystem']:08x}U",
        f"#define GU_BOARD_CHIP 0x{gpu['chip_id']:08x}U",
        f"#define GU_BOARD_SEGMENT {pci['segment']}U",
        f"#define GU_BOARD_BUS {pci['bus']}U",
        f"#define GU_BOARD_DEVICE {pci['device']}U",
        f"#define GU_BOARD_FUNCTION {pci['function']}U",
        f"#define GU_BOARD_ROOT_HID 0x{pci['root_hid']:08x}U",
        f"#define GU_BOARD_ROOT_UID 0x{pci['root_uid']:08x}U",
        f"#define GU_BOARD_ROOT_PORT_DEVICE {pci['root_port_device']}U",
        f"#define GU_BOARD_ROOT_PORT_FUNCTION {pci['root_port_function']}U",
        f"#define GU_BOARD_BAR0 0x{bar['base']:08x}U",
        f"#define GU_BOARD_BAR0_END 0x{bar['base'] + bar['size'] - 1:08x}U",
        f"#define GU_BOARD_BAR0_SIZE 0x{bar['size']:08x}U",
        f"#define GU_BOARD_FB_MB 0x{gpu['fb_mb']:x}U",
        f"#define GU_BOARD_PROM_SHA256 \"{firmware['prom_sha256']}\"",
        "",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes("\n".join(lines).encode("ascii"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="create JSON from probe output and validated PROM")
    create.add_argument("--probe", type=Path, required=True)
    create.add_argument("--prom", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    header = commands.add_parser("header", help="compile a JSON profile into a C header")
    header.add_argument("--profile", type=Path, required=True)
    header.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "create":
        create_profile(args.probe, args.prom, args.output)
    else:
        emit_header(args.profile, args.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        sys.exit(f"profile failed: {error}")
