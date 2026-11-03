#!/usr/bin/env python3
"""Validate locally read A540 firmware layout; this does not authenticate signatures."""
from pathlib import Path
import hashlib
import struct

FILES = ('a530_pm4.fw','a530_pfp.fw','a540_gpmu.fw2','a540_zap.mdt',
         'a540_zap.b00','a540_zap.b01','a540_zap.b02','a540_zap.elf')

def validate(directory):
    directory=Path(directory)
    files={name:(directory/name).read_bytes() for name in FILES}
    for name in FILES[:3]:
        data=files[name]
        if len(data)<16 or len(data)%4 or not any(data):raise ValueError(name+': invalid microcode size')
    elf=files['a540_zap.elf'];mdt=files['a540_zap.mdt']
    if len(elf)<52 or elf[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('zap: expected little endian ELF32')
    header=struct.unpack_from('<16sHHIIIIIHHHHHH',elf)
    _,etype,machine,version,_,phoff,_,_,ehsize,phsize,phnum,_,_,_=header
    if etype!=2 or machine!=164 or version!=1 or ehsize!=52 or phsize!=32 or phnum!=3:
        raise ValueError('zap: unexpected ELF header')
    end=phoff+phnum*phsize
    if phoff<ehsize or end>len(elf) or end>len(mdt) or elf[:end]!=mdt[:end]:
        raise ValueError('zap: invalid or inconsistent program headers')
    segments=[]
    for index in range(phnum):
        typ,offset,vaddr,paddr,filesz,memsz,flags,align=struct.unpack_from('<8I',elf,phoff+index*phsize)
        part=files[f'a540_zap.b{index:02d}']
        if len(part)!=filesz:raise ValueError(f'zap: segment {index} size mismatch')
        if typ==1:
            if filesz>memsz or offset>len(elf) or filesz>len(elf)-offset or elf[offset:offset+filesz]!=part:
                raise ValueError(f'zap: segment {index} outside ELF or differs')
        segments.append({'index':index,'type':typ,'file_bytes':filesz,'memory_bytes':memsz,
                         'address':hex(paddr),'flags':hex(flags),'alignment':hex(align)})
    if mdt!=files['a540_zap.b00']+files['a540_zap.b01']:
        raise ValueError('zap: metadata does not match split headers and hash segment')
    return {'files':{name:{'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()} for name,data in files.items()},
            'zap_segments':segments,'signature_verified':False}

if __name__=='__main__':
    import argparse,json
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);args=p.parse_args()
    print(json.dumps(validate(args.directory),indent=2))
