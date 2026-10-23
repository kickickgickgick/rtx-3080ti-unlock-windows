"""Validate the built executable, rather than infer properties from linker flags."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

def inspect(path, map_path):
    data = Path(path).read_bytes()
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    def u16(at): return struct.unpack_from('<H', data, at)[0]
    def u32(at): return struct.unpack_from('<I', data, at)[0]
    require(data[:2] == b'MZ', 'DOS signature')
    pe = u32(0x3c)
    require(data[pe:pe+4] == b'PE\0\0', 'PE signature')
    require(u16(pe+4) == 0x8664, 'must be AMD64')
    optional = pe+24
    require(u16(optional) == 0x20b, 'must be PE32+')
    require(u16(optional+68) == 10, 'must be EFI application')
    entry = u32(optional+16)
    require(entry != 0, 'entry point missing')
    require(struct.unpack_from('<Q', data, optional+24)[0] == 0, 'expected image base 0')
    require(u16(optional+70) & 0x140 == 0x140, 'ASLR and NX flags required')
    require(u32(optional+108) >= 16, 'data directory missing')
    for index, name in [(1, 'imports'), (12, 'IAT'), (13, 'delay imports')]:
        require(data[optional+112+8*index:optional+120+8*index] == b'\0'*8, name)
    require(u32(optional+112+8*5+4) != 0, 'relocations missing')
    table = optional+u16(pe+20)
    executable_entry = False
    for i in range(u16(pe+6)):
        at = table+40*i
        if u32(at+12) <= entry < u32(at+12)+u32(at+8):
            executable_entry = bool(u32(at+36) & 0x20000000)
    require(executable_entry, 'entry must be in executable section')
    text = Path(map_path).read_text()
    found = re.search(r'^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+efi_main\s+([0-9A-Fa-f]+)', text, re.M)
    require(found and int(found[1], 16) == entry, 'entry does not match efi_main map symbol')
    return dict(path=Path(path).name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                machine='AMD64', subsystem=10, entry='efi_main', entry_rva=entry,
                imports=0, iat=0, delay_imports=0, nx=True, dynamic_base=True)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('efi'); p.add_argument('map'); p.add_argument('--output', required=True)
    a = p.parse_args(); result = inspect(a.efi, a.map)
    Path(a.output).write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
