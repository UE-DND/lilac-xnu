#!/usr/bin/env python3
"""Read AArch64 PE code without rewriting malformed vendor debug directories."""
import argparse
from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('file', type=Path)
args = parser.parse_args()
data = args.file.read_bytes()
if data[:2] != b'MZ':
    parser.error('missing DOS signature')
pe = struct.unpack_from('<I', data, 0x3c)[0]
if data[pe:pe+4] != b'PE\0\0':
    parser.error('missing PE signature')
machine, count = struct.unpack_from('<HH', data, pe+4)
if machine != 0xaa64:
    parser.error('not AArch64')
optional_size = struct.unpack_from('<H', data, pe+20)[0]
optional = pe+24
base = struct.unpack_from('<Q', data, optional+24)[0]
decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
decoder.skipdata = True
for i in range(count):
    section = optional+optional_size+i*40
    name = data[section:section+8].rstrip(b'\0').decode(errors='replace')
    virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, section+8)
    flags = struct.unpack_from('<I', data, section+36)[0]
    if not flags & 0x20000000:
        continue
    if raw+raw_size > len(data):
        parser.error('section exceeds file')
    print('# section', name, 'RVA', hex(rva))
    for instruction in decoder.disasm(data[raw:raw+min(raw_size,virtual_size)], base+rva):
        print(f'{instruction.address:08x}: {instruction.mnemonic:8s} {instruction.op_str}')
