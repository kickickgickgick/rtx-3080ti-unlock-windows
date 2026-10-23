# SPDX-License-Identifier: GPL-2.0-only
import json
import tempfile
import unittest
from pathlib import Path

import profile_tool


VALID = {
    "format": profile_tool.FORMAT,
    "identity": 0x220810DE,
    "class_revision": 0x030000A1,
    "subsystem": 0x000110DE,
    "segment": 0,
    "bus": 1,
    "device": 0,
    "function": 0,
    "root_hid": 0x0A0341D0,
    "root_uid": 0,
    "root_port_device": 4,
    "root_port_function": 0,
    "bar0_base": 0xD0000000,
    "bar0_maximum": 0xD0FFFFFF,
    "bar0_size": 0x01000000,
    "bar0_translation": 0,
    "bar0_granularity": 32,
    "chip_id": 0xB72000A1,
    "fb_mb": 0x3000,
    "cold_ss0": 0x01234567,
    "cold_ss1": 7,
    "prom_size": 0x00100000,
    "gpu_writes": 0,
}


def cfg_text(values=VALID):
    lines = []
    for key, value in values.items():
        lines.append(f"{key}={value}" if key == "format" else f"{key}=0x{value:016x}")
    return "\r\n".join(lines) + "\r\n"


class ProfileToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.cfg = self.root / "PROFILE.CFG"
        self.prom = self.root / "PROM.BIN"
        self.profile = self.root / "local-profile.json"
        self.header = self.root / "gu_profile_generated.h"
        self.cfg.write_text(cfg_text(), encoding="ascii", newline="")
        self.prom.write_bytes(b"\x55\xaa" + bytes(profile_tool.SUPPORTED_PROM_SIZE - 2))

    def tearDown(self):
        self.temp.cleanup()

    def test_create_and_emit_header(self):
        profile_tool.create_profile(self.cfg, self.prom, self.profile)
        profile_tool.emit_header(self.profile, self.header)
        data = json.loads(self.profile.read_text(encoding="utf-8"))
        text = self.header.read_text(encoding="ascii")
        self.assertEqual(data["pci"]["bus"], 1)
        self.assertEqual(len(data["firmware"]["prom_sha256"]), 64)
        self.assertIn("#define GU_BOARD_BUS 1U", text)
        self.assertIn("#define GU_BOARD_BAR0 0xd0000000U", text)

    def test_unknown_and_duplicate_fields_are_rejected(self):
        self.cfg.write_text(cfg_text() + "bus=0x0000000000000008\r\n", encoding="ascii")
        with self.assertRaises(ValueError):
            profile_tool.parse_cfg(self.cfg)

    def test_wrong_gpu_and_bar_are_rejected(self):
        for key, value in (("identity", 0x220410DE), ("bar0_size", 0x2000000),
                           ("gpu_writes", 1), ("cold_ss1", 8),
                           ("subsystem", 0x100000001)):
            changed = dict(VALID); changed[key] = value
            self.cfg.write_text(cfg_text(changed), encoding="ascii", newline="")
            with self.assertRaises(ValueError, msg=key):
                profile_tool.create_profile(self.cfg, self.prom, self.profile)

    def test_prom_shape_is_rejected(self):
        self.prom.write_bytes(b"\x55\xaa" + bytes(510))
        with self.assertRaises(ValueError):
            profile_tool.create_profile(self.cfg, self.prom, self.profile)

    def test_profile_unknown_field_is_rejected(self):
        profile_tool.create_profile(self.cfg, self.prom, self.profile)
        data = json.loads(self.profile.read_text(encoding="utf-8"))
        data["unexpected"] = True
        self.profile.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaises(ValueError):
            profile_tool.emit_header(self.profile, self.header)

    def test_profile_duplicate_and_boolean_fields_are_rejected(self):
        profile_tool.create_profile(self.cfg, self.prom, self.profile)
        original = self.profile.read_text(encoding="utf-8")
        self.profile.write_text('{"schema": 1,' + original.lstrip()[1:], encoding="utf-8")
        with self.assertRaises(ValueError):
            profile_tool.emit_header(self.profile, self.header)
        profile_tool.create_profile(self.cfg, self.prom, self.profile)
        data = json.loads(self.profile.read_text(encoding="utf-8"))
        data["pci"]["segment"] = False
        self.profile.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaises(ValueError):
            profile_tool.emit_header(self.profile, self.header)
        data["pci"]["segment"] = 0
        data["schema"] = True
        self.profile.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaises(ValueError):
            profile_tool.emit_header(self.profile, self.header)


if __name__ == "__main__":
    unittest.main()
