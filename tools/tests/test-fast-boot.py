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
if manifest.get('buttons'): expected_args += ['lilac_buttons=1']
if manifest.get('display_mode') == 'scaled2': expected_args += ['fbscale=2']
if manifest.get('display_mode') == 'native': expected_args += ['fbwidth=720', 'fbheight=1280']
if manifest.get('gpu_mesa_dir'):
    expected_args += ['lilac_gpu_snapshot=1', 'lilac_gpu_cx=1', 'lilac_a540=7', 'gpu_cached=1']
if manifest.get('gpu_runtime_pm'): expected_args += ['lilac_gpu_pm=1']
if manifest.get('gpu_dynamic_clock'): expected_args += ['lilac_gpu_clock=1']
if manifest.get('return_after'):
    expected_args += ['loader_return_after=' + str(manifest['return_after'])]
assert shim['extra_boot_args'].split() == expected_args
assert bool(shim.get('usb_debug')) == bool(manifest.get('usb_debug'))
assert bool(shim.get('smp')) == bool(manifest.get('smp'))
assert manifest['image_bytes'] == image.stat().st_size <= 16*1024*1024
assert manifest['image_sha256'] == hashlib.sha256(image.read_bytes()).hexdigest()
assert (out/'root/probe/system-map.bin').read_bytes() == (root/'configs/lilac/system-map.bin').read_bytes()
assert not (out/'root/LaunchDaemons/com.lilac.port.services.plist').exists()
iconjob = plistlib.loads((out/'root/LaunchDaemons/com.apple.iconservices.iconservicesagent.plist').read_bytes())
assert iconjob['MachServices'].get('com.apple.iconservices') is True
assert iconjob['ProgramArguments'][0] == '/System/Library/CoreServices/iconservicesagent'
for label in ('com.apple.iconservices.iconservicesagent', 'com.apple.chronod',
              'com.apple.distnoted.xpc.daemon', 'com.apple.mobile.installd',
              'com.apple.pluginkit.pkd', 'com.apple.mobileassetd', 'com.apple.fseventsd',
              'com.apple.configd', 'com.apple.donotdisturbd',
              'com.apple.managedconfiguration.profiled', 'com.apple.misagent'):
    job = plistlib.loads((out/'root/LaunchDaemons'/f'{label}.plist').read_bytes())
    original = plistlib.loads((local_path('ios')/'rootfs/System/Library/LaunchDaemons'/f'{label}.plist').read_bytes())
    original['StandardErrorPath'] = '/dev/null'
    assert job == original, label
if manifest.get('ram_migration_state'):
    state = manifest['ram_migration_state']
    template = root/'sources/hakios/userland/var/mobile/Library/Preferences/com.apple.migration.plist'
    system = plistlib.loads((local_path('ios')/'rootfs/System/Library/CoreServices/SystemVersion.plist').read_bytes())
    assert state['build_version'] == system['ProductBuildVersion']
    assert state['migration_executed'] is False
    assert template.read_bytes() in (out/'root/sbin/launchd').read_bytes()
    assert b'RAM MIGRATION STATE:' in (out/'root/sbin/launchd').read_bytes()
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
    targets = b'backboardd SpringBoard' if manifest.get('desktop_bringup') else b'backboardd'
    assert b'/lilac-probe/lib/liblilac.touch.dylib ' + targets + b'\0' in static_program
    if manifest.get('desktop_bringup'):
        bridge = (out/'root/lib/liblilac.touch.dylib').read_bytes()
        assert b'20F66 hooks installed; RAM bring-up suppresses Setup' in bridge
        assert b'20F66 SpringBoard ABI mismatch; original Setup retained' in bridge
        source = root/'userland/springboard-bringup.c'
        assert manifest['inputs'][str(source)] == hashlib.sha256(source.read_bytes()).hexdigest()
    if manifest.get('home_gesture'):
        assert manifest['home_gesture'] == 'three-finger-tap'
        bridge = (out/'root/lib/liblilac.touch.dylib').read_bytes()
        assert b'three-finger tap sends Home after all fingers lift' in bridge
        assert b'lilac input: three-finger Home tap' in bridge
        header = root/'sources/hakios/userland/src/lilac-home-gesture.h'
        assert manifest['inputs'][str(header)] == hashlib.sha256(header.read_bytes()).hexdigest()
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
