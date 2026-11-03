#!/usr/bin/env python3
"""Validate a built fast-boot volume's required jobs, inputs and quiet settings."""
import argparse
import hashlib
import json
import plistlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
from lilaclib.paths import local_path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
a = p.parse_args()
out = a.build
manifest = json.loads((out/'manifest.json').read_text())
shim = json.loads((out/'image/manifest.json').read_text())
image = out/'image/xnu-boot.img'
assert manifest['fast_boot'] and manifest['mobile_runtime'] and manifest['desktop_attempt']
assert not manifest['mesa_library'] and shim['quiet_boot']
assert shim['extra_boot_args'].split() == ['lilac_ufs=1','lilac_quiet=1','lilac_ufs_fast=1']
assert manifest['image_bytes'] == image.stat().st_size <= 16*1024*1024
assert manifest['image_sha256'] == hashlib.sha256(image.read_bytes()).hexdigest()
assert (out/'root/probe/system-map.bin').read_bytes() == (root/'configs/lilac/system-map.bin').read_bytes()
assert not (out/'root/LaunchDaemons/com.lilac.port.services.plist').exists()
for label in ('com.apple.runningboardd','com.apple.backboardd','com.apple.SpringBoard'):
    job=plistlib.loads((out/'root/LaunchDaemons'/f'{label}.plist').read_bytes())
    assert job['RunAtLoad'] and job['MachServices'] and job['StandardErrorPath']=='/dev/null'
    assert job.get('EnvironmentVariables',{}).get('OS_ACTIVITY_DT_MODE') != 'YES'
for name in ('libobjc-trampolines.dylib','libffi-trampolines.dylib'):
    assert f'/desktop-usr-lib/{name}' in manifest['apfs_readback']
    assert (out/'root/desktop-usr-lib'/name).read_bytes() == (local_path('ios')/'cryptex/usr/lib'/name).read_bytes()
print('PASS: quiet boot image, system mapping, desktop jobs, APFS library readback and no diagnostic job')
