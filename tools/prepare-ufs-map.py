#!/usr/bin/env python3
"""Validate plaintext FIEMAP against backed-up GPT and emit read-only XNU map."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--fiemap',type=Path,required=True)
p.add_argument('--layout',type=Path,required=True)
p.add_argument('--image',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
m=json.loads(a.fiemap.read_text());layout=json.loads(a.layout.read_text());ud=layout['userdata']
assert not a.output.exists(),'output must be new'
size=a.image.stat().st_size
assert m['size']==size and size>0 and size%4096==0
assert not m['inode_flags']&(0x800|4|0x100000),'encrypted/compressed/verity image not supported'
assert m['mapped']==len(m['extents']) and 0<m['mapped']<=4096
assert m['extents'][-1]['flags']&1,'incomplete FIEMAP'
extents=[];logical=0
for e in m['extents']:
    assert e['logical']==logical and e['length']>0
    assert e['flags']&~1==0,'unknown, delayed, encrypted, unwritten or shared extent'
    assert not (e['logical']|e['physical']|e['length'])&4095
    length=min(e['length'],size-logical)
    assert length>0
    physical=e['physical']//4096+ud['first'];blocks=length//4096
    assert ud['first']<=physical<=ud['last'] and blocks<=ud['last']-physical+1
    assert all(physical+blocks<=q[1] or q[1]+q[2]<=physical for q in extents),'physical overlap'
    extents.append((logical//4096,physical,blocks));logical+=length
assert logical==size,'sparse/holey/incomplete image not supported'
with a.image.open('rb') as f:
    first=f.read(4096);f.seek(size-4096);last=f.read(4096)
assert first[32:36]==b'NXSB'
header=struct.pack('<4Q6I',0x31474d49434c494c,size,ud['first'],ud['last'],1,len(extents),layout['gpt_crc'],zlib.crc32(first),zlib.crc32(last),0)
data=header+b''.join(struct.pack('<3Q',*e) for e in extents)
a.output.write_bytes(data)
record={'map_bytes':len(data),'map_sha256':hashlib.sha256(data).hexdigest(),'image_bytes':size,
        'inode_flags':hex(m['inode_flags']),'encrypted':False,'gpt_crc':hex(layout['gpt_crc']),
        'userdata_first':ud['first'],'userdata_last':ud['last'],
        'first_crc':hex(zlib.crc32(first)),'last_crc':hex(zlib.crc32(last)),
        'extents':[{'logical_blocks':l,'absolute_lba':p,'blocks':n} for l,p,n in extents],
        'semantics':'immutable read-only file-backed virtual block device; no repartitioning or raw writes'}
a.output.with_suffix('.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:v for k,v in record.items() if k!='extents'},indent=2))
