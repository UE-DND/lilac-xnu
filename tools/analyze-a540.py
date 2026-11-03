#!/usr/bin/env python3
"""Inspect Lilac's GPU DTB resources and named firmware; performs no device access."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('dtb', type=Path)
a = p.parse_args()
from lilaclib.paths import HAKIOS
spec = importlib.util.spec_from_file_location('soc_args', HAKIOS/'tools/soc-args.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
blob = a.dtb.read_bytes()
nodes = module.nodes_of(blob)

def cells(value):
    if len(value) % 4:
        raise ValueError('unaligned cells')
    return list(struct.unpack('>'+'I'*(len(value)//4),value))

def strings(value):
    if not value or value[-1] != 0:
        raise ValueError('unterminated string list')
    return [x.decode('ascii') for x in value[:-1].split(b'\0')]

def one(path, prop):
    v = cells(nodes[path][prop])
    if len(v) != 1:
        raise ValueError(path+': invalid '+prop)
    return v[0]

def reg(path):
    vals = cells(nodes[path]['reg'])
    if len(vals) != 2 or vals[1] == 0:
        raise ValueError(path+': expected one address and one size cell')
    return {'base':hex(vals[0]),'size':hex(vals[1])}

handles = {}
for path, props in nodes.items():
    if 'phandle' in props:
        key=one(path,'phandle')
        if key in handles:
            raise ValueError('duplicate phandle')
        handles[key]=path

def resolve(ref):
    if ref not in handles:
        raise ValueError('unknown phandle '+hex(ref))
    return handles[ref]

gpu='/soc/qcom,kgsl-3d0@5000000'
iommu='/soc/qcom,kgsl-iommu'
cc='/soc/qcom,gpucc@5065000'
chip=one(gpu,'qcom,chipid')
if chip != 0x05040001 or 'qcom,kgsl-3d0' not in strings(nodes[gpu]['compatible']):
    raise ValueError('unexpected GPU; refusing to identify as this Lilac A540')
interrupts=cells(nodes[gpu]['interrupts'])
if len(interrupts)!=3 or interrupts[0]!=0:
    raise ValueError('expected GIC SPI')
clock_names=strings(nodes[gpu]['clock-names'])
clock_cells=cells(nodes[gpu]['clocks'])
clocks=[]
i=0
while i < len(clock_cells):
    provider=resolve(clock_cells[i]);n=one(provider,'#clock-cells');i+=1
    if i+n>len(clock_cells):raise ValueError('truncated clock specifier')
    clocks.append({'provider':provider,'specifier':[hex(x) for x in clock_cells[i:i+n]]});i+=n
if len(clocks)!=len(clock_names):raise ValueError('clock names and specifiers differ')
for name, item in zip(clock_names,clocks):item['name']=name
supply={name:resolve(one(gpu,name+'-supply')) for name in strings(nodes[gpu]['regulator-names'])}
levels=[]
prefix=gpu+'/qcom,gpu-pwrlevels/qcom,gpu-pwrlevel@'
for path in nodes:
    if path.startswith(prefix):
        levels.append({'level':one(path,'reg'),'hz':one(path,'qcom,gpu-freq'),'bus':one(path,'qcom,bus-freq')})
levels.sort(key=lambda x:x['level'])
if [x['level'] for x in levels]!=list(range(len(levels))) or not levels:
    raise ValueError('invalid power levels')
contexts={}
for name in ('gfx3d_user','gfx3d_secure'):
    path=iommu+'/'+name;vals=cells(nodes[path]['iommus'])
    if len(vals)<2:raise ValueError('missing IOMMU stream')
    contexts[name]={'provider':resolve(vals[0]),'stream_cells':[hex(v) for v in vals[1:]]}
    if 'qcom,gpu-offset' in nodes[path]:contexts[name]['gpu_offset']=hex(one(path,'qcom,gpu-offset'))
print(json.dumps({'dtb_sha256':hashlib.sha256(blob).hexdigest(), 'chip_id':hex(chip),
    'gpu':reg(gpu), 'gpucc':reg(cc), 'smmu':reg(iommu), 'irq':interrupts[1]+32,
    'gpmu_firmware':strings(nodes[gpu]['qcom,gpmu-firmware'])[0],
    'zap_name':strings(nodes[gpu]['qcom,zap-shader'])[0],
    'clocks':clocks,'regulators':supply,'levels':levels,'contexts':contexts},indent=2))
