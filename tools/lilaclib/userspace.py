#!/usr/bin/env python3
"""Build and read back a small APFS process/dyld diagnostic; host files only."""
import argparse
import hashlib
import gzip
import importlib.util
import json
import plistlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys

from .paths import ROOT, HAKIOS, DEPS, local_path
from . import ramboot

SYSIMAGE = local_path('ios')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def commands(path, kind):
    data = path.read_bytes()
    magic, cpu, subtype, filetype, count, size, flags, _ = struct.unpack_from('<8I', data)
    if (magic, cpu, subtype, filetype) != (0xfeedfacf, 0x100000c, 0, kind):
        raise ValueError(f'{path}: expected plain ARM64 Mach-O type {kind}')
    offset, result = 32, []
    for _ in range(count):
        cmd, length = struct.unpack_from('<II', data, offset)
        if length < 8 or length % 8 or offset + length > 32 + size:
            raise ValueError(f'{path}: invalid load command')
        result.append((cmd, data[offset:offset + length]))
        offset += length
    if offset != 32 + size or offset > len(data):
        raise ValueError(f'{path}: invalid command size')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new output directory')
    parser.add_argument('--dtb', type=Path, default=local_path('dtb'),
                        help='Sony device tree blob from the target device')
    parser.add_argument('--ufs-kernel-probe', action='store_true', help='test XNU native read-only devices instead of dyld child')
    parser.add_argument('--ufs-system-map',type=Path,help='verified plaintext UFS image map; adds mount/pager/full cache dynamic batch')
    parser.add_argument('--mobile-runtime',action='store_true',help='runtime/volatile var batch, pivot UFS root, real Apple PID1 launchd + notifyd/cfprefsd')
    parser.add_argument('--gpu-firmware-dir',type=Path,help='validated A540 firmware inputs to place on the RAM volume')
    parser.add_argument('--mesa-library',type=Path,help='cross-built OSMesa dylib to test from RAM')
    parser.add_argument('--gpu-mesa-dir', type=Path, help='experimental Mesa build output for a GPU-capable desktop')
    parser.add_argument('--desktop-attempt',action='store_true',help='start original desktop jobs once and report their process/log state')
    parser.add_argument('--fast-boot',action='store_true',help='skip passed diagnostics and boot desktop without the test dashboard')
    parser.add_argument('--desktop-bringup', action='store_true',
                        help='explicit RAM desktop: suppress unsupported Setup using guarded 20F66 SpringBoard hooks; no real activation')
    parser.add_argument('--ram-migration-state', action='store_true',
                        help='experimental A/B: seed the upstream migration version preference on disposable RAM /var; skips stock migration')
    parser.add_argument('--touchscreen', action='store_true', help='experimental S3330 QUP acquisition and backboardd HID bridge; requires desktop attempt')
    parser.add_argument('--buttons', action='store_true', help='experimental PM8998 volume and power buttons; requires touchscreen bridge')
    parser.add_argument('--display-mode', choices=('scaled2', 'iphone', 'native'), default='scaled2',
                        help='desktop rendering: 750x1624 (default), 1125x2436, or experimental 720x1280')
    parser.add_argument('--usb-debug', action='store_true', help='export logs after kernel return; retain touch bridge diagnostics')
    parser.add_argument('--return-after', type=int, default=0, help='return to BootShim after this many seconds of kernel runtime')
    parser.add_argument('--gpu-runtime-pm',action=argparse.BooleanOptionalAction,default=None,help='A540 idle collapse/resume; enabled by default for the GPU desktop')
    parser.add_argument('--gpu-dynamic-clock',action=argparse.BooleanOptionalAction,default=None,help='A540 180/257 MHz policy; enabled by default for the GPU desktop')
    parser.add_argument('--smp',action=argparse.BooleanOptionalAction,default=True,help='offer firmware-off CPU cores via PSCI; --no-smp selects one CPU')
    args = parser.parse_args()
    if not 0 <= args.return_after <= 3600:
        parser.error('--return-after must be between 0 and 3600')
    if args.desktop_bringup and not (args.fast_boot and args.touchscreen):
        parser.error('--desktop-bringup requires --fast-boot --touchscreen')
    if args.buttons and not args.touchscreen:
        parser.error('--buttons requires --touchscreen')
    if (args.gpu_runtime_pm or args.gpu_dynamic_clock) and not args.gpu_mesa_dir:
        parser.error('GPU power options require --gpu-mesa-dir')
    if args.ufs_system_map and not args.ufs_kernel_probe:
        parser.error('--ufs-system-map requires --ufs-kernel-probe')
    if args.mobile_runtime and not args.ufs_system_map:
        parser.error('--mobile-runtime requires --ufs-system-map')
    if args.mesa_library and not args.mobile_runtime:
        parser.error('--mesa-library requires --mobile-runtime')
    if args.desktop_attempt and not args.mobile_runtime:
        parser.error('--desktop-attempt requires --mobile-runtime')
    if args.ram_migration_state and not args.desktop_attempt:
        parser.error('--ram-migration-state requires --desktop-attempt')
    if args.fast_boot and (not args.desktop_attempt or args.mesa_library):
        parser.error('--fast-boot requires --desktop-attempt and excludes --mesa-library')
    if args.gpu_mesa_dir and (not args.fast_boot or not args.touchscreen or not args.gpu_firmware_dir):
        parser.error('--gpu-mesa-dir requires --fast-boot --touchscreen and --gpu-firmware-dir')
    if args.gpu_runtime_pm is None: args.gpu_runtime_pm = bool(args.gpu_mesa_dir)
    if args.gpu_dynamic_clock is None: args.gpu_dynamic_clock = bool(args.gpu_mesa_dir)
    if args.gpu_mesa_dir:
        from .hardware import validate_dtb, validate_gpu_render
        nodes = validate_dtb(args.dtb.read_bytes(), gpu_cx=True)
        validate_gpu_render(nodes)
    if args.touchscreen:
        if not args.desktop_attempt:
            parser.error('--touchscreen requires --desktop-attempt')
        from .hardware import validate_dtb
        validate_dtb(args.dtb.read_bytes())
    if args.buttons:
        from .hardware import validate_buttons, validate_dtb
        validate_buttons(validate_dtb(args.dtb.read_bytes()))
    if args.fast_boot and args.return_after:
        print(f'WARNING: timed diagnostic only: returns after {args.return_after}s from kernel startup, '
              'even if the desktop is still loading; omit --return-after for interactive use.', flush=True)
    args.output.mkdir(parents=True, exist_ok=False)
    out = args.output.resolve()
    root = out / 'root'
    for directory in ('sbin', 'bin', 'usr/lib', 'dev', 'private/var', 'probe','ios'):
        (root / directory).mkdir(parents=True, exist_ok=True)
    for directory in [root, *[p for p in root.rglob('*') if p.is_dir()]]:
        directory.chmod(0o755)
    obj = out / 'obj'
    obj.mkdir()
    gpu_inputs=[]
    if args.gpu_firmware_dir:
        from tools import a540_firmware
        gpu_report=a540_firmware.validate(args.gpu_firmware_dir)
        destination=root/'probe/gpu-firmware'
        destination.mkdir(mode=0o755)
        for name in a540_firmware.FILES:
            source=args.gpu_firmware_dir/name
            data=source.read_bytes()
            if hashlib.sha256(data).hexdigest()!=gpu_report['files'][name]['sha256']:
                raise ValueError(f'{name}: changed during validation')
            (destination/name).write_bytes(data)
            gpu_inputs.append(source.resolve())
        gpu_inputs.append(ROOT/'tools/a540_firmware.py')
    mobile_inputs=[]
    migration_state = None
    if args.mobile_runtime:
        for directory in ('LaunchDaemons','empty'):
            (root/directory).mkdir(mode=0o755)
        skeleton=SYSIMAGE/'var.skel'
        dirs=[];links=[]
        for line in skeleton.read_text().splitlines():
            fields=line.split('\t')
            if fields[0]=='d':
                assert len(fields)==5 and fields[4].startswith('/private/var/') and len(fields[4])<450
                dirs.append((fields[4],int(fields[1],8),int(fields[2]),int(fields[3])))
            elif fields[0]=='l':
                assert len(fields)==3 and fields[2].startswith('/private/var/') and len(fields[2])<450
                links.append((fields[2],fields[1]))
            else:raise ValueError('unexpected var skeleton record')
        assert len(dirs)>50
        (obj/'var-skeleton.h').write_text(
            'static const struct {const char *path;int mode,uid,gid;} var_dirs[]={\n'+
            ''.join('{%s,0%o,%d,%d},\n'%(json.dumps(path),mode,uid,gid) for path,mode,uid,gid in dirs)+'};\n'+
            'static const struct {const char *path,*target;} var_links[]={\n'+
            ''.join('{%s,%s},\n'%(json.dumps(path),json.dumps(target)) for path,target in links)+'};\n')
        if args.ram_migration_state:
            from .ramstate import migration_header
            template = HAKIOS/'userland/var/mobile/Library/Preferences/com.apple.migration.plist'
            system_version = SYSIMAGE/'rootfs/System/Library/CoreServices/SystemVersion.plist'
            migration_state = migration_header(template, system_version, obj/'ram-migration-state.h')
            mobile_inputs += [template, system_version, ROOT/'tools/lilaclib/ramstate.py']
        for label in ('com.apple.notifyd','com.apple.cfprefsd.xpc.daemon'):
            source=SYSIMAGE/'rootfs/System/Library/LaunchDaemons'/f'{label}.plist'
            job=plistlib.loads(source.read_bytes())
            binary=job.get('Program') or job['ProgramArguments'][0]
            assert (SYSIMAGE/'rootfs'/binary.lstrip('/')).is_file()
            job['StandardOutPath']='/dev/null' if args.fast_boot else '/dev/console'
            job['StandardErrorPath']=job['StandardOutPath']
            (root/'LaunchDaemons'/f'{label}.plist').write_bytes(plistlib.dumps(job))
            mobile_inputs += [source,SYSIMAGE/'rootfs'/binary.lstrip('/')]
        if args.desktop_attempt:
            # These original OS cryptex libraries are absent from the selected
            # UFS tree. Keep them in the RAM volume for a read-only union mount.
            libdir=root/'desktop-usr-lib'
            libdir.mkdir(mode=0o755)
            for name in ('libobjc-trampolines.dylib','libffi-trampolines.dylib','libstdc++.6.0.9.dylib'):
                source=SYSIMAGE/'cryptex/usr/lib'/name
                commands(source,6)
                shutil.copyfile(source,libdir/name)
                (libdir/name).chmod(0o755)
                mobile_inputs.append(source)
            # The APFS host reader does not dereference symlinks for `cat`.
            # Separate identical files allow the builder to verify each path.
            for name in ('libstdc++.6.dylib','libstdc++.dylib'):
                shutil.copyfile(libdir/'libstdc++.6.0.9.dylib',libdir/name)
                (libdir/name).chmod(0o755)
            for label in ('com.apple.containermanagerd','com.apple.containermanagerd.system',
                          'com.apple.mobile.usermanagerd','com.apple.mobilegestalt.xpc',
                          'com.apple.lsd','com.apple.logd'):
                source=SYSIMAGE/'rootfs/System/Library/LaunchDaemons'/f'{label}.plist'
                job=plistlib.loads(source.read_bytes())
                binary=job.get('Program') or job['ProgramArguments'][0]
                executable=SYSIMAGE/'rootfs'/binary.lstrip('/')
                if not executable.is_file():raise ValueError(f'missing desktop support executable: {binary}')
                job.pop('_LimitLoadToDeviceTree',None)
                job.pop('LimitLoadFromHardware',None)
                job['KeepAlive']=False
                job['StandardErrorPath']='/dev/null' if args.fast_boot else '/dev/console'
                (root/'LaunchDaemons'/f'{label}.plist').write_bytes(plistlib.dumps(job))
                mobile_inputs += [source,executable]
            # Services requested by the observed desktop/LaunchServices session.
            # Keep the vendor's demand-launch, credentials and load restrictions:
            # these are distinct from the three one-shot desktop bring-up jobs.
            for label in ('com.apple.iconservices.iconservicesagent', 'com.apple.chronod',
                          'com.apple.distnoted.xpc.daemon', 'com.apple.mobile.installd',
                          'com.apple.pluginkit.pkd', 'com.apple.mobileassetd',
                          'com.apple.fseventsd', 'com.apple.configd',
                          'com.apple.donotdisturbd', 'com.apple.managedconfiguration.profiled',
                          'com.apple.misagent'):
                source = SYSIMAGE/'rootfs/System/Library/LaunchDaemons'/f'{label}.plist'
                job = plistlib.loads(source.read_bytes())
                binary = job.get('Program') or job['ProgramArguments'][0]
                executable = SYSIMAGE/'rootfs'/binary.lstrip('/')
                if not executable.is_file(): raise ValueError(f'missing desktop service executable: {binary}')
                job['StandardErrorPath'] = '/dev/null' if args.fast_boot else '/dev/console'
                (root/'LaunchDaemons'/f'{label}.plist').write_bytes(plistlib.dumps(job))
                mobile_inputs += [source, executable]
            for label, short in (('com.apple.runningboardd','runningboard'),
                                 ('com.apple.backboardd','backboard'),
                                 ('com.apple.SpringBoard','springboard')):
                source=SYSIMAGE/'rootfs/System/Library/LaunchDaemons'/f'{label}.plist'
                job=plistlib.loads(source.read_bytes())
                binary=job.get('Program') or job['ProgramArguments'][0]
                executable=SYSIMAGE/'rootfs'/binary.lstrip('/')
                if not executable.is_file():raise ValueError(f'missing desktop executable: {binary}')
                # Attempt each real job once: retain MachServices and credentials, but
                # do not let a crashing daemon restart indefinitely in RAM.
                job.pop('LimitLoadFromHardware',None)
                job.pop('_LimitLoadFromClarityMode',None)
                job['RunAtLoad']=True;job['KeepAlive']=False
                job['StandardErrorPath']='/dev/null' if args.fast_boot else f'/private/var/log/lilac-{short}.log'
                if not args.fast_boot:job.setdefault('EnvironmentVariables',{})['OS_ACTIVITY_DT_MODE']='YES'
                if args.touchscreen and label == 'com.apple.backboardd':
                    if args.usb_debug:
                        job.setdefault('EnvironmentVariables', {})['LILAC_TOUCH_DEBUG'] = '1'
                (root/'LaunchDaemons'/f'{label}.plist').write_bytes(plistlib.dumps(job))
                mobile_inputs += [source,executable]
            mobile_inputs += [ROOT/'userland/desktop-watch.c',ROOT/'userland/desktop-libs.c']
        job={'Label':'com.lilac.port.services','ProgramArguments':['/lilac-probe/bin/apfs_probe','launchd-job'],
             'RunAtLoad':True,'AbandonProcessGroup':True,'ProcessType':'Interactive',
             'StandardOutPath':'/dev/console','StandardErrorPath':'/dev/console'}
        if not args.fast_boot:
            (root/'LaunchDaemons/com.lilac.port.services.plist').write_bytes(plistlib.dumps(job))
        mobile_inputs += [skeleton,ROOT/'userland/mobile-probe.h',ROOT/'userland/runtime-probe.c',
                          ROOT/'userland/foundation-probe.m',ROOT/'userland/services-probe.c',
                          ROOT/'userland/display-probe.c',ROOT/'userland/graphics-probe.c',ROOT/'userland/graphics-present.h',
                          ROOT/'userland/layer-probe.m',ROOT/'kernel/lilac-display-api.h',
                          ROOT/'bootloader/font_8x16.c',ROOT/'bootloader/font_8x16.h',
                          HAKIOS/'userland/src/surf_test.c']

    def run(command, logfile):
        with (out / logfile).open('w') as log:
            subprocess.run([str(v) for v in command], stdout=log, stderr=subprocess.STDOUT, check=True)

    desktop_sources = [ROOT/'userland/springboard-bringup.c'] if args.desktop_bringup else []
    if args.touchscreen:
        library = root / 'lib/liblilac.touch.dylib'
        library.parent.mkdir(mode=0o755)
        run(['bash', '-c', 'set -eu; source "$1"; shift; exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"',
             'lilac-input-build', HAKIOS / 'xnu-env.sh', '-arch', 'arm64', '-march=armv8-a', '-O1',
             '-dynamiclib', '-DLILAC_INPUT_ONLY=1', '-install_name', '/lilac-probe/lib/liblilac.touch.dylib',
             '-framework', 'CoreFoundation', '-framework', 'IOKit', '-lobjc',
             HAKIOS / 'userland/src/inject.c', *desktop_sources, '-o', library], 'touch-bridge-build.log')
        library.chmod(0o755)
        commands(library, 6)
        mobile_inputs += [HAKIOS / 'userland/src/inject.c', HAKIOS / 'userland/src/hakios.input.h',
                          HAKIOS / 'userland/src/lilac-home-gesture.h',
                          ROOT / 'userland/touch-bridge.h',
                          ROOT / 'tools/lilaclib/hardware.py', *desktop_sources]

    if args.gpu_mesa_dir:
        mesa_dir = args.gpu_mesa_dir.resolve()
        mesa_library = mesa_dir / 'libOSMesa.8.dylib'
        registry, exports, built_from = (mesa_dir / name for name in ('gl.xml', 'exported-gl.txt', 'built-from'))
        patches = HAKIOS / 'tools/mesa'
        patch_inputs = sorted(patches.glob('*.patch')) + sorted(path for path in (patches / 'hakios-drm').iterdir() if path.is_file())
        patch_digest = hashlib.sha256(b''.join(path.read_bytes() for path in patch_inputs)).hexdigest()
        if built_from.read_text().split()[0] != patch_digest:
            raise ValueError('GPU Mesa output does not match the current Darwin patches')
        commands(mesa_library, 6)
        mesa_copy = obj / 'mesa.dylib'
        shutil.copyfile(mesa_library, mesa_copy)
        install_tool = local_path('llvm') / 'llvm-install-name-tool'
        run([install_tool, '-id', '/private/var/g/mesa.dylib', mesa_copy], 'mesa-install-name.log')
        dylib_ids = [c[struct.unpack_from('<I', c, 8)[0]:].split(b'\0')[0].decode()
                     for cmd, c in commands(mesa_copy, 6) if cmd == 0xd]
        if dylib_ids != ['/private/var/g/mesa.dylib']:
            raise ValueError('GPU Mesa install name did not change as requested')
        mesa_bytes = mesa_copy.read_bytes()
        if len(mesa_bytes) < 1024 or len(mesa_bytes) > 32 * 1024 * 1024:
            raise ValueError('GPU Mesa library size is outside the supported range')
        (root/'probe/gpu-mesa.gz').write_bytes(gzip.compress(mesa_bytes, mtime=0))
        with (obj/'eagl_gen.h').open('wb') as generated:
            subprocess.run([sys.executable, str(HAKIOS/'userland/mkeagl.py'),
                            str(HAKIOS/'userland/src/eagl_slots.txt'), str(registry), str(exports),
                            str(HAKIOS/'userland/src/eagl.c')], stdout=generated, check=True)
        gl_library = root/'lib/liblilac.gl.dylib'
        run(['bash', '-c', 'set -eu; source "$1"; shift; exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"',
             'lilac-gl-build', HAKIOS/'xnu-env.sh', '-arch', 'arm64', '-march=armv8-a', '-O2',
             '-dynamiclib', '-install_name', '/lilac-probe/lib/liblilac.gl.dylib', '-I'+str(obj),
             '-framework', 'CoreFoundation', '-framework', 'IOKit', '-framework', 'IOSurface', '-lobjc',
             HAKIOS/'userland/src/eagl.c', HAKIOS/'userland/src/gl_osmesa.c', mesa_copy, '-o', gl_library], 'gpu-gl-build.log')
        commands(gl_library, 6)
        gl_library.chmod(0o755)
        library = root/'lib/liblilac.touch.dylib'
        run(['bash', '-c', 'set -eu; source "$1"; shift; exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"',
             'lilac-gpu-input-build', HAKIOS/'xnu-env.sh', '-arch', 'arm64', '-march=armv8-a', '-O1',
             '-dynamiclib', '-DLILAC_GPU_INPUT=1', '-DHAKIOS_GL="/lilac-probe/lib/liblilac.gl.dylib"',
             '-install_name', '/lilac-probe/lib/liblilac.touch.dylib', '-framework', 'CoreFoundation',
             '-framework', 'IOKit', '-lobjc', HAKIOS/'userland/src/inject.c', *desktop_sources, '-o', library], 'gpu-input-build.log')
        commands(library, 6)
        library.chmod(0o755)
        run(['bash', '-c', 'set -eu; source "$1"; shift; exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"',
             'lilac-gpu-unpack-build', HAKIOS/'xnu-env.sh', '-arch', 'arm64', '-march=armv8-a', '-O1',
             '-DLILAC_GPU_MESA_BYTES='+str(len(mesa_bytes)), ROOT/'userland/gpu-unpack.c', '-lz', '-o', obj/'gpu_unpack'], 'gpu-unpack-build.log')
        commands(obj/'gpu_unpack', 2)
        run(['bash', '-c', 'set -eu; source "$1"; shift; exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"',
             'lilac-gpu-probe-build', HAKIOS/'xnu-env.sh', '-arch', 'arm64', '-march=armv8-a', '-O1',
             '-DLILAC_EXPECT_CPUS='+('8' if args.smp else '1'), ROOT/'userland/gpu-probe.c', mesa_copy, '-framework', 'IOKit', '-o', obj/'gpu_probe'], 'gpu-probe-build.log')
        commands(obj/'gpu_probe', 2)
        mobile_inputs += [mesa_library, registry, exports, built_from, *patch_inputs,
                          HAKIOS/'userland/mkeagl.py', HAKIOS/'userland/src/eagl_slots.txt',
                          HAKIOS/'userland/src/eagl.c', HAKIOS/'userland/src/gl_osmesa.c',
                          HAKIOS/'userland/src/hakios.gl.h', ROOT/'userland/gpu-unpack.c', ROOT/'userland/gpu-probe.c', ROOT/'userland/gpu-isolation-probe.h', ROOT/'userland/smp-probe.h', ROOT/'kernel/lilac-a5xx-pm4.h']

    run(['bash', '-c', '''set -eu
source "$1"
shift
exec xnu-clang "$@"
''', 'lilac-probe-build', HAKIOS / 'xnu-env.sh',
         '-arch', 'arm64', '-march=armv8-a', '-Os', '-static', '-nostdlib', '-ffreestanding',
         '-fno-stack-protector', '-fno-builtin', '-mstrict-align', '-mgeneral-regs-only',
         *(['-DLILAC_UFS_PROBE=1'] if args.ufs_kernel_probe else []),
         *(['-DLILAC_SYSTEM_PROBE=1'] if args.ufs_system_map else []),
         *(['-DLILAC_MOBILE_PROBE=1','-I'+str(obj)] if args.mobile_runtime else []),
         *(['-DLILAC_MESA_PROBE=1'] if args.mesa_library else []),
         *(['-DLILAC_DESKTOP_ATTEMPT=1'] if args.desktop_attempt else []),
         *(['-DLILAC_FAST_BOOT=1'] if args.fast_boot else []),
         '-DLILAC_DESKTOP_BRINGUP='+str(int(args.desktop_bringup)),
         *(['-DLILAC_RAM_MIGRATION_STATE=1'] if args.ram_migration_state else []),
         *(['-DLILAC_TOUCHSCREEN=1'] if args.touchscreen else []),
         *(['-DLILAC_GPU_GL=1'] if args.gpu_mesa_dir else []),
         '-I' + str(HAKIOS / 'userland/libmini'), '-Wl,-e,_start', '-Wl,-pagezero_size,0x100000000',
         ROOT / 'userland/probe-init.c', HAKIOS / 'userland/libmini/mini.c',
         HAKIOS / 'userland/libmini/start.S', '-o', obj / 'probe-init'], 'static-build.log')
    run(['bash', '-c', '''set -eu
source "$1"
shift
exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"
''', 'lilac-dynamic-build', HAKIOS / 'xnu-env.sh',
         '-arch', 'arm64', '-march=armv8-a', '-O1', HAKIOS / 'userland/src/hello_dyn.c',
         '-o', obj / 'hello_dyn'], 'dynamic-build.log')
    if args.mobile_runtime and not args.fast_boot:
        for source,name,flags in [(ROOT/'userland/runtime-probe.c','runtime_probe',['-fblocks']),
                                  (ROOT/'userland/foundation-probe.m','foundation_probe',['-fobjc-arc','-framework','Foundation']),
                                  (ROOT/'userland/services-probe.c','services_probe',['-framework','CoreFoundation']),
                                  (HAKIOS/'userland/src/surf_test.c','surface_probe',['-framework','CoreFoundation','-framework','IOSurface']),
                                  (ROOT/'userland/display-probe.c','display_probe',['-framework','CoreFoundation','-framework','IOSurface','-framework','IOKit',str(ROOT/'bootloader/font_8x16.c')]),
                                  (ROOT/'userland/graphics-probe.c','graphics_probe',['-framework','CoreFoundation','-framework','CoreGraphics','-framework','CoreText','-framework','IOSurface','-framework','IOKit']),
                                  (ROOT/'userland/layer-probe.m','layer_probe',['-fobjc-arc','-DLILAC_UIKIT=0','-framework','Foundation','-framework','QuartzCore','-framework','CoreGraphics','-framework','IOSurface','-framework','IOKit']),
                                  (ROOT/'userland/layer-probe.m','uikit_probe',['-fobjc-arc','-DLILAC_UIKIT=1','-framework','Foundation','-framework','UIKit','-framework','QuartzCore','-framework','CoreGraphics','-framework','IOSurface','-framework','IOKit'])]:
            run(['bash','-c','''set -eu
source "$1"
shift
exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"
''','lilac-mobile-build',HAKIOS/'xnu-env.sh','-arch','arm64','-march=armv8-a','-O1',
                 *(['-DLILAC_DESKTOP_ATTEMPT=1'] if args.desktop_attempt and name=='display_probe' else []),
                 *flags,source,'-o',obj/name],name+'-build.log')
        if args.mesa_library:
            run(['bash','-c','''set -eu
source "$1"
shift
exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"
''','lilac-mesa-build',HAKIOS/'xnu-env.sh','-arch','arm64','-march=armv8-a','-O1',
                 ROOT/'userland/mesa-probe.c','-lz','-o',obj/'mesa_probe'],'mesa_probe-build.log')
        if args.desktop_attempt:
            for source,name in ((ROOT/'userland/desktop-watch.c','desktop_watch'),
                                (ROOT/'userland/desktop-libs.c','desktop_libs')):
                run(['bash','-c','''set -eu
source "$1"
shift
exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"
''','lilac-desktop-build',HAKIOS/'xnu-env.sh','-arch','arm64','-march=armv8-a','-O1',
                     source,'-o',obj/name],name+'-build.log')

    static = ramboot.macho((obj / 'probe-init').read_bytes())
    dynamic = commands(obj / 'hello_dyn', 2)
    dylinker = [c[struct.unpack_from('<I', c, 8)[0]:].split(b'\0')[0].decode()
                for cmd, c in dynamic if cmd == 0xe]
    dylibs = [c[struct.unpack_from('<I', c, 8)[0]:].split(b'\0')[0].decode()
              for cmd, c in dynamic if cmd == 0xc]
    if dylinker != ['/usr/lib/dyld'] or dylibs != ['/usr/lib/libSystem.B.dylib']:
        raise ValueError(f'unexpected dynamic dependencies: {dylinker}, {dylibs}')
    dyld = SYSIMAGE / 'rootfs/usr/lib/dyld'
    commands(dyld, 7)
    sources = {
        'sbin/launchd': obj / 'probe-init',
        'bin/apfs_probe': obj / 'probe-init',
        'bin/hello_dyn': obj / 'hello_dyn',
        'usr/lib/dyld': dyld,
    }
    if args.gpu_mesa_dir:
        sources['bin/gpu_unpack'] = obj/'gpu_unpack'
        sources['bin/gpu_probe'] = obj/'gpu_probe'
    if args.mobile_runtime and not args.fast_boot:
        for name in ('runtime_probe','foundation_probe','services_probe','surface_probe','display_probe','graphics_probe','layer_probe','uikit_probe'):
            commands(obj/name,2)
            sources['bin/'+name]=obj/name
        if args.mesa_library:
            commands(obj/'mesa_probe',2)
            sources['bin/mesa_probe']=obj/'mesa_probe'
            commands(args.mesa_library,6)
            data=args.mesa_library.read_bytes()
            if len(data)>32*1024*1024:raise ValueError('Mesa library exceeds RAM limit')
            (root/'probe/libOSMesa.8.dylib.gz').write_bytes(gzip.compress(data,mtime=0))
            gpu_inputs += [args.mesa_library.resolve(),ROOT/'userland/mesa-probe.c']
        if args.desktop_attempt:
            for name in ('desktop_watch','desktop_libs'):
                commands(obj/name,2)
                sources['bin/'+name]=obj/name
    for name, source in sources.items():
        shutil.copyfile(source, root / name)
        (root / name).chmod(0o755)
    data = bytes((i * 73 + (i >> 8) * 19 + 41) & 255 for i in range(4 * 16384 + 37))
    (root / 'probe/pattern.bin').write_bytes(data)
    (root / 'probe/pattern.bin').chmod(0o644)
    if args.ufs_system_map:
        shutil.copyfile(args.ufs_system_map,root/'probe/system-map.bin')
        (root/'probe/system-map.bin').chmod(0o400)
    run([sys.executable, HAKIOS / 'tools/apfs/mkapfs.py', '-o', out / 'root.apfs',
         'LilacUser=' + str(root)], 'apfs-build.log')
    ramboot.apfs((out / 'root.apfs').read_bytes())
    reader = DEPS / 'host/bin/apfs-tool'
    checked = {}
    for path in sorted(root.rglob('*')):
        if path.is_file():
            name = '/' + str(path.relative_to(root))
            readback = subprocess.check_output([str(reader), 'cat', str(out / 'root.apfs'), name])
            if readback != path.read_bytes():
                raise ValueError(f'APFS readback differs: {name}')
            checked[name] = {'bytes': len(readback), 'sha256': sha(path)}
    run([reader, 'census', out / 'root.apfs'], 'apfs-census.tsv')
    extra_boot_args = (('lilac_ufs=1 lilac_quiet=1 lilac_ufs_fast=1' if args.fast_boot else 'lilac_ufs=1')
                       if args.ufs_kernel_probe else 'systrace=2 systrace_name=hello_dyn')
    if args.touchscreen:
        extra_boot_args += ' lilac_touch=1'
    if args.buttons:
        extra_boot_args += ' lilac_buttons=1'
    if args.desktop_attempt:
        if args.display_mode == 'scaled2': extra_boot_args += ' fbscale=2'
        elif args.display_mode == 'native': extra_boot_args += ' fbwidth=720 fbheight=1280'
    if args.gpu_mesa_dir:
        extra_boot_args += ' lilac_gpu_snapshot=1 lilac_gpu_cx=1 lilac_a540=7 gpu_cached=1'
    if args.gpu_runtime_pm: extra_boot_args += ' lilac_gpu_pm=1'
    if args.gpu_dynamic_clock: extra_boot_args += ' lilac_gpu_clock=1'
    if args.return_after:
        extra_boot_args += ' loader_return_after=' + str(args.return_after)
    run([sys.executable, '-m', 'tools.lilaclib.ramboot', '--kernel', args.kernel.resolve(),
         '--ramdisk-format', 'apfs', '--ramdisk', out / 'root.apfs', '--dtb', args.dtb.resolve(),
         *(['--quiet-boot'] if args.fast_boot else []),
         *(['--smp'] if args.smp else []),
         *(['--usb-debug'] if args.usb_debug else []),
         '--extra-boot-args', extra_boot_args,
         '--output', out / 'image'], 'image-build.log')
    inputs = [ROOT / 'userland/probe-init.c', Path(__file__).resolve(),
              HAKIOS / 'userland/libmini/mini.c', HAKIOS / 'userland/libmini/start.S',
              HAKIOS / 'userland/libmini/mini.h', HAKIOS / 'userland/src/hello_dyn.c',
              HAKIOS / 'xnu-env.sh', DEPS / 'host/bin/xnu-clang',
              DEPS / 'host/bin/ld64', HAKIOS / 'tools/apfs/mkapfs.py', reader,
              dyld, SYSIMAGE / 'input-provenance.json']
    if args.ufs_kernel_probe:
        inputs += [ROOT/'userland/ufs-probe.h', ROOT/'kernel/lilac-ufs-api.h']
    if args.ufs_system_map:
        inputs += [ROOT/'userland/system-probe.h',ROOT/'kernel/lilac-image.h',args.ufs_system_map.resolve()]
    inputs += mobile_inputs
    inputs += gpu_inputs
    manifest = {
        'gpu_runtime_pm': args.gpu_runtime_pm, 'gpu_dynamic_clock': args.gpu_dynamic_clock,
        'touchscreen': args.touchscreen,
        'buttons': args.buttons,
        'display_mode': args.display_mode if args.desktop_attempt else None,
        'home_gesture': 'three-finger-tap' if args.touchscreen else None,
        'desktop_bringup': args.desktop_bringup,
        'usb_debug': args.usb_debug,
        'return_after': args.return_after,
        'ram_migration_state': migration_state,
        'purpose': ('fast desktop boot via verified UFS image and Apple launchd' if args.fast_boot else 'resident PID1: '+('mobile runtime, RAM var, UFS root pivot and real Apple PID1 launchd/services' if args.mobile_runtime else 'full UFS APFS/cache mount/paging/dynamic batch' if args.ufs_system_map else 'native UFS device batch' if args.ufs_kernel_probe else 'APFS/process/Apple dyld missing dependency diagnostic')),
        'static_entry': static['entry'], 'dynamic_linker': dylinker, 'dynamic_libraries': dylibs,
        'shared_cache_included': False,
        'shared_cache_on_UFS':bool(args.ufs_system_map),
        'dynamic_limit': ('Original complete cache is on UFS; actual mount/dynamic execution remains pending.' if args.ufs_system_map else
                          'libSystem is absent from this RAM volume; full dynamic-program success is not expected.'),
        'apfs_readback': checked,
        'inputs': {str(p): sha(p) for p in inputs},
        'image_bytes': (out / 'image/xnu-boot.img').stat().st_size,
        'image_sha256': sha(out / 'image/xnu-boot.img'),
        'device_validation': 'pending',
        'ufs_kernel_probe': args.ufs_kernel_probe,
        'ufs_system_map':str(args.ufs_system_map.resolve()) if args.ufs_system_map else None,
        'mobile_runtime':args.mobile_runtime,
        'gpu_firmware':bool(args.gpu_firmware_dir),
        'mesa_library':bool(args.mesa_library),
        'gpu_mesa_dir': str(args.gpu_mesa_dir.resolve()) if args.gpu_mesa_dir else None,
        'desktop_attempt':bool(args.desktop_attempt),
        'fast_boot':bool(args.fast_boot),
        'smp':bool(args.smp),
        'mobile_plan':(['verify UFS map and mount system read only','RAM-only var and cryptex library overlay','Apple launchd and selected desktop jobs; no test dashboard'] if args.fast_boot else ['original mount/pager/dynamic and native UFS regression','RAM-only writable var with original template',
                       'eight isolated bounded runtime cases','pivot existing read-only UFS volume as real root',
                       'Apple launchd replaces PID1, only two stock service jobs and probe',
                       'IOSurface cross-process sharing, properties, seeds, Mach ports and YUV planes',
                       'native/scaled/filtered/clear scanout readback and live IOSurface dashboard',
                       'CoreGraphics bitmap and alpha, CoreText glyphs, and framework-drawn IOSurface scanout',
                       'isolated CoreAnimation layer tree and UIKit view/label offscreen rendering and scanout',
                       'independent observer keeps summaries and failure log tail visible'] if args.mobile_runtime else []),
    }
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'output': str(out), 'image_bytes': manifest['image_bytes'],
                      'image_sha256': manifest['image_sha256'], 'readback_files': len(checked)}))


if __name__ == '__main__':
    main()
