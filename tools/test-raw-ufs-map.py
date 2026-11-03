#!/usr/bin/env python3
"""Exercise raw-userdata map preparation and refusal of unsafe inputs."""
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lilac-raw-map-') as tmp:
    p=Path(tmp);image=bytearray(3*4096);image[32:36]=b'NXSB';image[-1]=42
    (p/'image').write_bytes(image);sha=hashlib.sha256(image).hexdigest()
    head=struct.pack('<4Q6I',0x31474d49434c494c,len(image),100,199,1,2,0x12345678,
                     zlib.crc32(image[:4096]),zlib.crc32(image[-4096:]),0)
    good=head+struct.pack('<3Q',0,110,1)+struct.pack('<3Q',1,120,2)
    def run(name,ref=good,digest=sha):
        (p/'ref').write_bytes(ref);out=p/name
        result=subprocess.run(['python3',str(root/'tools/prepare-raw-ufs-map.py'),
            '--reference-map',str(p/'ref'),'--image',str(p/'image'),'--expected-sha256',digest,
            '--output',str(out)],capture_output=True)
        return result.returncode,out
    status,out=run('valid');assert status==0
    values=struct.unpack('<4Q6I3Q',out.read_bytes())
    assert values[5]==1 and values[-3:]==(0,100,3)
    for i,ref in enumerate((good[:-1],head+struct.pack('<3Q',0,110,1)+struct.pack('<3Q',1,110,2),
                            head+struct.pack('<3Q',0,110,1)+struct.pack('<3Q',1,199,2))):
        status,out=run('bad'+str(i),ref);assert status and not out.exists()
    status,out=run('bad_hash',digest='0'*64);assert status and not out.exists()
print('PASS: raw userdata map, extent bounds/overlap, exact length and image hash refusal')
