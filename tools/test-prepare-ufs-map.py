#!/usr/bin/env python3
"""Reject unsafe Android FIEMAP inputs before any XNU mapping is generated."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='prepare-map-') as tmp:
    d=Path(tmp);image=d/'system.apfs';b=bytearray(16384);b[32:36]=b'NXSB';image.write_bytes(b)
    layout={'gpt_crc':123,'userdata':{'first':100,'last':199}}
    (d/'layout.json').write_text(json.dumps(layout))
    baseline={'size':16384,'inode_flags':0x80010,'mapped':2,'extents':[
        {'logical':0,'physical':4096*10,'length':8192,'flags':0},
        {'logical':8192,'physical':4096*30,'length':8192,'flags':1}]}
    cases=[]
    def change(name,fn):
        m=copy.deepcopy(baseline);fn(m);cases.append((name,m))
    change('encrypted inode',lambda m:m.update(inode_flags=0x80810))
    change('compressed inode',lambda m:m.update(inode_flags=4))
    change('verity inode',lambda m:m.update(inode_flags=0x100000))
    change('size mismatch',lambda m:m.update(size=4096))
    change('count mismatch',lambda m:m.update(mapped=1))
    change('missing last',lambda m:m['extents'][-1].update(flags=0))
    change('delalloc',lambda m:m['extents'][0].update(flags=4))
    change('unwritten',lambda m:m['extents'][0].update(flags=0x800))
    change('shared',lambda m:m['extents'][0].update(flags=0x2000))
    change('logical hole',lambda m:m['extents'][1].update(logical=12288))
    change('unaligned',lambda m:m['extents'][0].update(physical=4097))
    change('physical overlap',lambda m:m['extents'][1].update(physical=4096*11))
    change('outside userdata',lambda m:m['extents'][1].update(physical=4096*100))
    for i,(name,m) in enumerate([('valid',baseline)]+cases):
        f=d/f'{i}.json';f.write_text(json.dumps(m));out=d/f'{i}.bin'
        p=subprocess.run(['python3',str(ROOT/'tools/prepare-ufs-map.py'),'--fiemap',str(f),'--layout',str(d/'layout.json'),
                          '--image',str(image),'--output',str(out)],capture_output=True,text=True)
        if i==0:
            assert p.returncode==0,p.stderr
            values=struct.unpack_from('<4Q6I',out.read_bytes())
            assert values[0]==0x31474d49434c494c and values[1]==16384 and values[5]==2
            assert struct.unpack_from('<3Q',out.read_bytes(),56)==(0,110,2)
        else:assert p.returncode!=0 and not out.exists(),name
    print(f'PASS: valid exact wire map and {len(cases)} unsafe FIEMAP variants rejected before output')
