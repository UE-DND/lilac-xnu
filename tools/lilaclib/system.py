#!/usr/bin/env python3
"""Build a disk-resident APFS volume with the original, unsplit iOS cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from .paths import ROOT, HAKIOS, DEPS, OUT, local_path
from .files import copy_file
SYSIMAGE=local_path('ios')
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--probe-binary',type=Path,default=OUT/'latest/userspace/obj/hello_dyn')
a=p.parse_args()
if not a.probe_binary.is_file():
    p.error('missing hello_dyn: run ./lilac build userspace first, or pass --probe-binary')
a.output.mkdir(parents=True,exist_ok=False);out=a.output.resolve()
source=SYSIMAGE/'rootfs';cache=SYSIMAGE/'20F66__iPhone10,3_6'
shutil.copytree(source,out/'root',copy_function=copy_file,symlinks=True)
cacheout=out/'root/System/Library/Caches/com.apple.dyld';cacheout.mkdir(parents=True)
caches=sorted(cache.glob('dyld_shared_cache_arm64*'))
assert len(caches)==43
for f in caches:copy_file(f,cacheout/f.name)
copy_file(a.probe_binary.resolve(),out/'root/bin/hello_dyn')
probe=out/'root/lilac-probe';probe.mkdir()
pattern=bytes((i*73+(i>>8)*19+41)&255 for i in range(65573))
(probe/'pattern.bin').write_bytes(pattern)
with (out/'build.log').open('w') as log:
    subprocess.run(['python3',str(HAKIOS/'tools/apfs/mkapfs.py'),'-o',str(out/'system.apfs'),
                    'LilacSystem='+str(out/'root')+',manifest='+str(SYSIMAGE/'system.manifest')],
                   stdout=log,stderr=subprocess.STDOUT,check=True)
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(4<<20),b''):h.update(chunk)
    return h.hexdigest()
reader=DEPS/'host/bin/apfs-tool'
checked={}
for name in ['/usr/lib/dyld','/sbin/launchd','/bin/hello_dyn','/lilac-probe/pattern.bin']:
    data=subprocess.check_output([str(reader),'cat',str(out/'system.apfs'),name])
    assert data==(out/'root'/name[1:]).read_bytes(),name
    checked[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
# Read all 43 cache files back, hashing a stream instead of loading 2.72GB.
cache_info=[]
for f in caches:
    path='/System/Library/Caches/com.apple.dyld/'+f.name
    proc=subprocess.Popen([str(reader),'cat',str(out/'system.apfs'),path],stdout=subprocess.PIPE)
    h=hashlib.sha256();n=0
    while True:
        chunk=proc.stdout.read(4<<20)
        if not chunk:break
        h.update(chunk);n+=len(chunk)
    assert proc.wait()==0 and n==f.stat().st_size and h.hexdigest()==digest(f),f.name
    cache_info.append({'name':f.name,'bytes':n,'sha256':h.hexdigest()})
manifest={'purpose':'original selected iOS root tree and complete unchanged 43-file shared cache; APFS disk image for UFS file-backed storage',
          'original_shared_cache_not_split':True,'cache_files':cache_info,'cache_bytes':sum(i['bytes'] for i in cache_info),
          'source_tree':str(source),'source_manifest_sha256':digest(SYSIMAGE/'system.manifest'),
          'apfs_readback':checked,'image_bytes':(out/'system.apfs').stat().st_size,
          'image_sha256':digest(out/'system.apfs'),'device_writes':'not performed by this builder',
          'inputs':{str(f):digest(f) for f in [Path(__file__).resolve(),HAKIOS/'tools/apfs/mkapfs.py',reader,a.probe_binary.resolve()]}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k not in ('cache_files','inputs')},indent=2))
