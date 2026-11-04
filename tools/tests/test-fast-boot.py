#!/usr/bin/env python3
"""Validate a built fast-boot volume's required jobs, inputs and quiet settings."""
import argparse
import hashlib
import gzip
import json
import plistlib
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lilaclib.paths import local_path
from lilaclib.userspace import commands
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
a = p.parse_args()
out = a.build
manifest = json.loads((out/'manifest.json').read_text())
shim = json.loads((out/'image/manifest.json').read_text())
image = out/'image/xnu-boot.img'
assert manifest['fast_boot'] and manifest['mobile_runtime'] and manifest['desktop_attempt']
assert not manifest['mesa_library'] and shim['quiet_boot']
expected_args = ['lilac_ufs=1','lilac_quiet=1','lilac_ufs_fast=1'] + (['lilac_touch=1'] if manifest.get('touchscreen') else [])
if manifest.get('gpu_mesa_dir'):
    expected_args += ['lilac_gpu_snapshot=1', 'lilac_gpu_cx=1', 'lilac_a540=7', 'gpu_cached=1']
if manifest.get('return_after'):
    expected_args += ['loader_return_after=' + str(manifest['return_after'])]
assert shim['extra_boot_args'].split() == expected_args
assert bool(shim.get('usb_debug')) == bool(manifest.get('usb_debug'))
assert manifest['image_bytes'] == image.stat().st_size <= 16*1024*1024
assert manifest['image_sha256'] == hashlib.sha256(image.read_bytes()).hexdigest()
assert (out/'root/probe/system-map.bin').read_bytes() == (root/'configs/lilac/system-map.bin').read_bytes()
assert not (out/'root/LaunchDaemons/com.lilac.port.services.plist').exists()
for label in ('com.apple.runningboardd','com.apple.backboardd','com.apple.SpringBoard'):
    job=plistlib.loads((out/'root/LaunchDaemons'/f'{label}.plist').read_bytes())
    assert job['RunAtLoad'] and job['MachServices'] and job['StandardErrorPath']=='/dev/null'
    assert job.get('EnvironmentVariables',{}).get('OS_ACTIVITY_DT_MODE') != 'YES'
    assert 'DYLD_INSERT_LIBRARIES' not in job.get('EnvironmentVariables', {})
    if manifest.get('touchscreen') and label == 'com.apple.backboardd':
        assert job['EnvironmentVariables'].get('LILAC_TOUCH_DEBUG') == ('1' if manifest.get('usb_debug') else None)
if manifest.get('touchscreen'):
    assert '/lib/liblilac.touch.dylib' in manifest['apfs_readback']
    static_program = (out/'root/sbin/launchd').read_bytes()
    assert b'kern.exec_inject\0' in static_program
    assert b'/lilac-probe/lib/liblilac.touch.dylib backboardd\0' in static_program
if manifest.get('gpu_mesa_dir'):
    mesa = gzip.decompress((out/'root/probe/gpu-mesa.gz').read_bytes())
    assert mesa == (out/'obj/mesa.dylib').read_bytes()
    assert '/probe/gpu-mesa.gz' in manifest['apfs_readback']
    assert '/lib/liblilac.gl.dylib' in manifest['apfs_readback']
    assert '/bin/gpu_unpack' in manifest['apfs_readback']
    assert '/bin/gpu_probe' in manifest['apfs_readback']
    assert b'/lilac-probe/bin/gpu_probe\0' in (out/'root/sbin/launchd').read_bytes()
    commands(out/'root/bin/gpu_probe', 2)
    def names(path, kind):
        return [data[struct.unpack_from('<I', data, 8)[0]:].split(b'\0')[0].decode()
                for cmd, data in commands(path, 6) if cmd == kind]
    assert names(out/'obj/mesa.dylib', 0xd) == ['/private/var/g/mesa.dylib']
    assert '/private/var/g/mesa.dylib' in names(out/'root/lib/liblilac.gl.dylib', 0xc)
    bridge = (out/'root/lib/liblilac.touch.dylib').read_bytes()
    assert b'HakiosGPU\0' in bridge and b'/lilac-probe/lib/liblilac.gl.dylib\0' in bridge
for name in ('libobjc-trampolines.dylib','libffi-trampolines.dylib'):
    assert f'/desktop-usr-lib/{name}' in manifest['apfs_readback']
    assert (out/'root/desktop-usr-lib'/name).read_bytes() == (local_path('ios')/'cryptex/usr/lib'/name).read_bytes()
print('PASS: quiet boot image, system mapping, desktop jobs, APFS library readback and no diagnostic job')
