#!/usr/bin/env python3
"""Prepare a candidate mapping for an APFS image at userdata LBA 0; never writes a device.

The reference map supplies a previously validated GPT layout, not proof of a
current layout or of a successful flash. Verify both independently on-device.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import zlib

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference-map',type=Path,required=True)
p.add_argument('--image',type=Path,required=True)
p.add_argument('--expected-sha256',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if not re.fullmatch(r'[0-9a-f]{64}',a.expected_sha256):p.error('expected SHA-256 must be 64 lowercase hex digits')
if a.output.exists():p.error('output already exists')
ref=a.reference_map.read_bytes()
if len(ref)<56:raise ValueError('short reference map')
magic,size,first,last,version,count,gpt,first_crc,last_crc,reserved=struct.unpack_from('<4Q6I',ref)
if (magic!=0x31474d49434c494c or version!=1 or reserved or not 1<=count<=4096 or
    len(ref)!=56+count*24 or not size or size%4096 or first>last or size//4096>last-first+1):
    raise ValueError('invalid reference map or userdata bounds')
logical=0;physical=[]
for i in range(count):
    start,block,blocks=struct.unpack_from('<3Q',ref,56+i*24)
    if start!=logical or not blocks or block<first or block>last or blocks>last-block+1 or blocks>size//4096-logical:
        raise ValueError('invalid reference extent')
    if any(block<old+length and old<block+blocks for old,length in physical):
        raise ValueError('overlapping reference extent')
    physical.append((block,blocks));logical+=blocks
if logical!=size//4096 or a.image.stat().st_size!=size:raise ValueError('image length mismatch')
digest=hashlib.sha256()
with a.image.open('rb') as f:
    head=f.read(4096);digest.update(head)
    while chunk:=f.read(4*1024*1024):digest.update(chunk)
    f.seek(size-4096);tail=f.read(4096)
if (digest.hexdigest()!=a.expected_sha256 or head[32:36]!=b'NXSB' or
    zlib.crc32(head)!=first_crc or zlib.crc32(tail)!=last_crc):
    raise ValueError('image SHA-256, APFS magic or reference boundary CRC mismatch')
data=struct.pack('<4Q6I3Q',magic,size,first,last,version,1,gpt,first_crc,last_crc,0,0,first,size//4096)
with a.output.open('xb') as f:f.write(data)
print(f'Prepared candidate map: {size} bytes, userdata start LBA {first}, GPT CRC {gpt:08x}; no device accessed')
