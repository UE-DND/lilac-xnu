#!/usr/bin/env python3
"""Build a Sony RAM boot image on the host; never communicates with a device."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

from .paths import ROOT, HAKIOS, DEPS, local_path
BASE = 0x80080000
VIRT = 0xFFFFFFF004000000
PHYS = 0xA0000000
LIMIT = 0xF8000000
RAMDISK_VM_PAGE = 16384


def align(n, size=4096):
    return (n + size - 1) & -size


def macho(data, kernel=False):
    if len(data) < 32:
        raise ValueError("truncated Mach-O header")
    magic, cpu, subtype, kind, count, length, flags, reserved = struct.unpack_from('<8I', data)
    if (magic, cpu, kind) != (0xFEEDFACF, 0x100000C, 2):
        raise ValueError("expected an ARM64 MH_EXECUTE Mach-O")
    end = 32 + length
    if end > len(data):
        raise ValueError("truncated load commands")
    off, entry, high = 32, None, 0
    executable = []
    for _ in range(count):
        if off + 8 > end:
            raise ValueError("truncated load command")
        cmd, size = struct.unpack_from('<II', data, off)
        if size < 8 or size % 8 or off + size > end:
            raise ValueError("invalid load-command extent")
        if cmd == 0x19:
            if size < 72:
                raise ValueError("short LC_SEGMENT_64")
            va, vs, fo, fs, maxprot, prot = struct.unpack_from('<4Q2I', data, off + 24)
            if fs > vs or fo + fs > len(data):
                raise ValueError("segment extends beyond Mach-O")
            if kernel and vs:
                pa = va - VIRT + PHYS
                if pa < PHYS or pa + vs > LIMIT:
                    raise ValueError("kernel segment outside XNU RAM window")
                high = max(high, pa + vs)
            if prot & 4:
                executable.append((va, va + vs))
        elif cmd == 5:
            if size < 288 or struct.unpack_from('<II', data, off + 8) != (6, 68):
                raise ValueError("expected ARM_THREAD_STATE64")
            entry = struct.unpack_from('<Q', data, off + 16 + 32 * 8)[0]
        elif not kernel and cmd in (0xE, 0xC, 0x80000018, 0x8000001F):
            raise ValueError("PID 1 probe must not depend on dyld or dylibs")
        off += size
    if off != end or entry is None or not any(a <= entry < b for a, b in executable):
        raise ValueError("invalid load commands or non-executable entry")
    return {"entry": hex(entry), "physical_end": high}


def apfs(data):
    """Check the raw container boundary and primary superblock, not the file tree."""
    if len(data) < 4096 or data[32:36] != b'NXSB':
        raise ValueError("expected a raw APFS container (not a DMG or GPT disk)")
    block_size, blocks = struct.unpack_from('<IQ', data, 36)
    if block_size != 4096 or blocks == 0 or blocks * block_size != len(data):
        raise ValueError("APFS container size does not match its superblock")
    s1 = s2 = 0
    for word in struct.unpack_from('<1022I', data, 8):
        s1 = (s1 + word) % 0xffffffff
        s2 = (s2 + s1) % 0xffffffff
    c1 = 0xffffffff - ((s1 + s2) % 0xffffffff)
    c2 = 0xffffffff - ((s1 + c1) % 0xffffffff)
    if struct.unpack_from('<Q', data)[0] != (c2 << 32) | c1:
        raise ValueError("APFS primary superblock checksum mismatch")
    return {'format': 'apfs', 'block_size': block_size, 'blocks': blocks,
            'validation': 'primary superblock only; file contents require a separate reader'}


def ufs_snapshot_dt(dtb):
    """Fail closed unless the fixed MMIO mapping matches this Sony device tree."""
    spec = importlib.util.spec_from_file_location('soc', HAKIOS / 'tools/soc-args.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    nodes = module.nodes_of(dtb)
    soc, ufs = nodes.get('/soc', {}), nodes.get('/soc/ufshc@1da4000', {})
    if (nodes.get('', {}).get('#address-cells') != struct.pack('>I', 2)
            or soc.get('#address-cells') != struct.pack('>I', 1)
            or soc.get('#size-cells') != struct.pack('>I', 1)
            or soc.get('ranges') != struct.pack('>4I', 0, 0, 0, 0xffffffff)
            or b'qcom,ufshc' not in ufs.get('compatible', b'').split(b'\0')
            or ufs.get('reg') != struct.pack('>2I', 0x01da4000, 0x2500)
            or ufs.get('status', b'okay\0') not in (b'ok\0', b'okay\0')):
        raise ValueError('UFS snapshot requires the verified Sony 0x01da4000/0x2500 identity mapping')
    return {'node': '/soc/ufshc@1da4000', 'base': '0x01da4000', 'size': '0x2500',
            'mode': 'one pass of whitelisted register reads; halt before XNU',
            'storage_commands': False, 'controller_writes': False}


def validate_smp_dt(dtb):
    """Do not use the hard-coded topology on a different DTB or PSCI conduit."""
    spec = importlib.util.spec_from_file_location('soc', HAKIOS/'tools/soc-args.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    nodes = module.nodes_of(dtb)
    if nodes.get('/psci',{}).get('method') != b'smc\0':
        raise ValueError('SMP requires the Sony PSCI SMC conduit')
    expected = {0,1,2,3,0x100,0x101,0x102,0x103}
    seen = set()
    for path, props in nodes.items():
        if not path.startswith('/cpus/cpu@') or '/' in path[len('/cpus/'):]:
            continue
        if props.get('device_type') != b'cpu\0' or props.get('enable-method') != b'psci\0':
            raise ValueError('unexpected CPU node '+path)
        reg=props.get('reg',b'')
        if len(reg)!=8:
            raise ValueError('unexpected CPU MPIDR encoding '+path)
        value=struct.unpack('>Q',reg)[0]
        if value in seen:
            raise ValueError('duplicate CPU MPIDR')
        seen.add(value)
    if seen!=expected:
        raise ValueError('CPU topology does not match Sony MSM8998')
    return sorted(seen)


def validate_usb_debug_dt(dtb):
    spec = importlib.util.spec_from_file_location('usb_soc', HAKIOS / 'tools/soc-args.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    validate_usb_debug_nodes(module.nodes_of(dtb))


def validate_usb_debug_nodes(nodes):
    expected = {
        ('', '#address-cells'): struct.pack('>I', 2),
        ('/soc', '#address-cells'): struct.pack('>I', 1),
        ('/soc', '#size-cells'): struct.pack('>I', 1),
        ('/soc', 'ranges'): struct.pack('>4I', 0, 0, 0, 0xffffffff),
        ('/psci', 'method'): b'smc\0',
        ('/soc/ssusb@a800000', 'reg'): struct.pack('>4I', 0xa800000, 0xf8c00, 0xc016000, 0x400),
        ('/soc/ssusb@a800000', 'ranges'): b'',
        ('/soc/ssusb@a800000', '#address-cells'): struct.pack('>I', 1),
        ('/soc/ssusb@a800000', '#size-cells'): struct.pack('>I', 1),
        ('/soc/ssusb@a800000/dwc3@a800000', 'compatible'): b'snps,dwc3\0',
        ('/soc/ssusb@a800000/dwc3@a800000', 'reg'): struct.pack('>2I', 0xa800000, 0xcd00),
        ('/soc/ssusb@a800000/dwc3@a800000', 'maximum-speed'): b'high-speed\0',
        ('/soc/qusb@c012000', 'reg'): struct.pack('>6I', 0xc012000, 0x2a8, 0x1fcb24c, 4, 0x784238, 4),
        ('/soc/qcom,msm-imem@146bf000', '#address-cells'): struct.pack('>I', 1),
        ('/soc/qcom,msm-imem@146bf000', '#size-cells'): struct.pack('>I', 1),
        ('/soc/qcom,msm-imem@146bf000', 'ranges'): struct.pack('>3I', 0, 0x146bf000, 0x1000),
        ('/soc/qcom,msm-imem@146bf000/restart_reason@65c', 'reg'): struct.pack('>2I', 0x65c, 4),
        ('/soc/restart@10ac000', 'reg'): struct.pack('>4I', 0x10ac000, 4, 0x1fd3000, 4),
        ('/soc/restart@10ac000', 'compatible'): b'qcom,pshold\0',
        ('/soc/qcom,spmi@800f000', 'compatible'): b'qcom,spmi-pmic-arb\0',
        ('/soc/qcom,spmi@800f000', 'reg'): struct.pack('>10I', 0x800f000, 0x1000, 0x8400000, 0x1000000,
                                                    0x9400000, 0x1000000, 0xa400000, 0x220000, 0x800a000, 0x3000),
        ('/soc/qcom,spmi@800f000', 'reg-names'): b'core\0chnls\0obsrvr\0intr\0cnfg\0',
        ('/soc/qcom,spmi@800f000', 'qcom,ee'): struct.pack('>I', 0),
        ('/soc/qcom,spmi@800f000', 'qcom,reserved-chan'): struct.pack('>I', 511),
        ('/soc/qcom,spmi@800f000/qcom,pm8998@0', 'reg'): struct.pack('>2I', 0, 0),
        ('/soc/qcom,spmi@800f000/qcom,pm8998@0/qcom,power-on@800', 'reg'): struct.pack('>2I', 0x800, 0x100),
        ('/soc/qcom,spmi@800f000/qcom,pm8998@0/qcom,power-on@800', 'compatible'): b'qcom,qpnp-power-on\0',
        ('/soc/qcom,spmi@800f000/qcom,pm8998@0/qcom,power-on@800', 'qcom,system-reset'): b'',
        ('/soc/qcom,spmi@800f000/qcom,pm8998@0/qcom,power-on@800', 'qcom,store-hard-reset-reason'): b'',
    }
    for (node, key), value in expected.items():
        if nodes.get(node, {}).get(key) != value:
            raise ValueError(f'USB debug requires verified Lilac DTB: {node}/{key}')
    for node in {node for node, key in expected}:
        if nodes[node].get('status', b'okay\0') not in (b'ok\0', b'okay\0'):
            raise ValueError('USB debug refuses disabled node: ' + node)
    for node, properties in nodes.items():
        if b'qcom,qpnp-power-on' not in properties.get('compatible', b'').split(b'\0'):
            continue
        if any(key in properties for key in ('qcom,secondary-pon-reset', 'qcom,warm-reset-poweroff-type')):
            raise ValueError('USB debug refuses unhandled PON reset override: ' + node)
        if 'qcom,system-reset' in properties and node != '/soc/qcom,spmi@800f000/qcom,pm8998@0/qcom,power-on@800':
            raise ValueError('USB debug refuses additional system-reset PMIC: ' + node)


def main():
    p = argparse.ArgumentParser(prog='./lilac debug ramboot', description=__doc__)
    p.add_argument('--output', type=Path, required=True, help='new output directory')
    p.add_argument('--kernel', type=Path, required=True)
    p.add_argument('--ramdisk', type=Path, help='explicit payload; default builds userland/src/hello.c')
    p.add_argument('--ramdisk-format', choices=('macho', 'apfs'), default='macho')
    p.add_argument('--extra-boot-args', default='', help='append diagnostic kernel arguments for RAMDisk boot')
    p.add_argument('--quiet-boot',action='store_true',help='suppress normal loader text and kernel trace/verbose boot')
    p.add_argument('--smp',action='store_true',help='offer PSCI-off cores from the validated Sony CPU topology')
    p.add_argument('--usb-debug', action='store_true', help='serve captured logs over USB after XNU returns; RAM only')
    p.add_argument('--usb-self-test', action='store_true', help='test USB log retrieval without starting XNU')
    diagnostics = p.add_mutually_exclusive_group()
    diagnostics.add_argument('--ufs-snapshot', action='store_true',
                   help='read only UFS status registers and halt in BootShim; does not enter XNU')
    diagnostics.add_argument('--ufs-read-probe', action='store_true',
                   help='LUN 0 read-only capacity, full GPT and repeated partition boundary batch; borrows idle DMA pages, then halts')
    diagnostics.add_argument('--ufs-handoff-snapshot', action='store_true',
                   help='observe inherited UFS slot0 and queue stability after ABL; no commands or queue writes')
    p.add_argument('--dtb', type=Path, default=local_path('dtb'))
    p.add_argument('--llvm', type=Path, default=local_path('llvm', os.environ.get('LILAC_LLVM')))
    a = p.parse_args()
    if any(ord(c) < 32 or ord(c) > 126 for c in a.extra_boot_args):
        p.error('--extra-boot-args must contain printable ASCII only')
    if a.ramdisk_format == 'apfs' and a.ramdisk is None:
        p.error('--ramdisk-format apfs requires --ramdisk')
    a.usb_debug = a.usb_debug or a.usb_self_test
    if a.usb_debug and (a.ufs_snapshot or a.ufs_read_probe or a.ufs_handoff_snapshot):
        p.error('USB debug requires non-UFS-diagnostic RAM boot')
    kernel, dtb = a.kernel.read_bytes(), a.dtb.read_bytes()
    km = macho(kernel, True)
    if len(dtb) < 40 or struct.unpack_from('>I', dtb)[0] != 0xD00DFEED:
        raise ValueError("invalid Sony DTB")
    if a.smp:
        validate_smp_dt(dtb)
    if a.usb_debug:
        validate_usb_debug_dt(dtb)
    ufs_diagnostic = ufs_snapshot_dt(dtb) if a.ufs_snapshot or a.ufs_read_probe or a.ufs_handoff_snapshot else None
    if a.ufs_read_probe:
        ufs_diagnostic.update(mode='LUN 0 capacity, MBR, primary/repeated/backup GPT headers and full arrays, repeated partition boundaries; halt before XNU',
                              storage_commands=['READ CAPACITY(10)', 'READ(10)'],
                              controller_writes=['UTRLRSR: 0 or 1', 'UTRLDBR: 1'],
                              storage_writes=False, dma_buffer='stopped adjacent firmware TM list page',
                              retired_descriptor='completed coherent READ(10) or FUA WRITE(10); never resubmitted',
                              max_commands=512, max_partitions=128, max_entry_array_bytes=131072)
    if a.ufs_handoff_snapshot:
        ufs_diagnostic.update(mode='post-ABL slot0 descriptor/request/response/PRDT and two-pass queue stability; halt before XNU',
                              storage_writes=False, firmware_ram_writes=False,
                              old_data_pointer_dereferenced=False)
    a.output.mkdir(parents=True, exist_ok=False)
    out = a.output.resolve()
    hello_inputs = []
    hello_origin = 'explicit --ramdisk'
    if a.ramdisk is None:
        # Match HakiOS's static Mach-O recipe without running its APFS build.
        hello_source = HAKIOS / 'userland/src/hello.c'
        hello_env = HAKIOS / 'xnu-env.sh'
        hello_command = ['bash', '-c',
                         'source "$1" && xnu-clang -arch arm64 -march=armv8-a '
                         '-Os -static -nostdlib -ffreestanding -fno-stack-protector '
                         '-fno-builtin -mstrict-align -mgeneral-regs-only '
                         '-Wl,-e,_start -Wl,-pagezero_size,0x100000000 -o "$2" "$3"',
                         'lilac-hello', str(hello_env), str(out / 'hello'), str(hello_source)]
        subprocess.run(hello_command, check=True)
        a.ramdisk = out / 'hello'
        hello_inputs = [hello_source, hello_env,
                        DEPS / 'host/bin/xnu-clang',
                        DEPS / 'host/bin/ld64']
        hello_origin = 'built from sources/hakios/userland/src/hello.c'
    ramdisk = a.ramdisk.read_bytes()
    rm = macho(ramdisk) if a.ramdisk_format == 'macho' else apfs(ramdisk)
    flags = ['--target=aarch64-none-elf', '-march=armv8-a', '-ffreestanding', '-fno-builtin',
             '-fno-stack-protector', '-mstrict-align', '-mgeneral-regs-only', '-O2', '-Wall', '-Wextra']
    flags += ['-DLILAC_EXTRA_BOOT_ARGS=' + json.dumps(a.extra_boot_args)]
    usb_debug = None
    if a.usb_debug:
        debug_sources = [ROOT / 'bootloader' / name for name in
                         ('debug-usb.c', 'debug-usb.h', 'debug-protocol.h', 'debug-reboot.h', 'shim-entry.S', 'shim-main.c', 'smp-topology.h', 'smp-return.h')]
        run_id = hashlib.sha256(kernel + ramdisk + dtb + a.extra_boot_args.encode() +
                                bytes([a.usb_self_test, a.smp]) + b''.join(path.read_bytes() for path in debug_sources)).hexdigest()[:32]
        flags += ['-DLILAC_USB_DEBUG=1', '-DLILAC_DEBUG_RUN_ID=' + json.dumps(run_id)]
        if a.usb_self_test:
            flags.append('-DLILAC_USB_SELF_TEST=1')
        usb_debug = {'run_id': run_id, 'serial': 'lilac-debug', 'self_test': a.usb_self_test,
                     'mode': 'post-return log upload; no live XNU USB console', 'storage_writes': False}
    if a.quiet_boot:
        flags.append('-DLILAC_QUIET_BOOT=1')
    if a.smp:
        flags.append('-DLILAC_SMP=1')
    if a.ufs_snapshot:
        flags.append('-DLILAC_UFS_SNAPSHOT=1')
    if a.ufs_read_probe:
        flags.append('-DLILAC_UFS_READ=1')
    if a.ufs_handoff_snapshot:
        flags.append('-DLILAC_UFS_HANDOFF=1')
    source_names = ['shim-entry.S', 'shim-main.c', 'font_8x16.c']
    if a.usb_debug:
        source_names.append('debug-usb.c')
    if a.ufs_read_probe:
        source_names.append('ufs-read.c')
    if a.ufs_handoff_snapshot:
        source_names.append('ufs-handoff.c')
    objects = []
    for name in source_names:
        obj = out / (Path(name).stem + '.o')
        subprocess.run([str(a.llvm / 'clang'), *flags, '-c', str(ROOT / 'bootloader' / name), '-o', str(obj)], check=True)
        objects.append(str(obj))
    elf = out / 'shim.elf'
    subprocess.run([str(a.llvm / 'clang'), '--target=aarch64-none-elf', '-fuse-ld=lld', '-nostdlib',
                    '-Wl,-T,' + str(ROOT / 'bootloader/shim.ld'), *objects, '-o', str(elf)], check=True)
    subprocess.run([str(a.llvm / 'llvm-objcopy'), '-O', 'binary', str(elf), str(out / 'shim.bin')], check=True)
    symbols = subprocess.check_output([str(a.llvm / 'llvm-nm'), str(elf)], text=True)
    payload_offset = next(int(line.split()[0], 16) - BASE for line in symbols.splitlines()
                          if line.split()[-1] == '_payload_start')
    shim = (out / 'shim.bin').read_bytes()
    if len(shim) > payload_offset or payload_offset % 4096:
        raise ValueError("invalid linker payload boundary")
    rd_pa = align(km['physical_end'], 16384) + 0x10000
    rd_memory_bytes = align(len(ramdisk), RAMDISK_VM_PAGE)
    top_bound = align(rd_pa + rd_memory_bytes, 16384) + 0x80000 + 0x8000
    if top_bound > LIMIT:
        raise ValueError("RAMDisk and metadata exceed available RAM")
    payload = struct.pack('<4I', 0x4C494C43, len(kernel), len(ramdisk), 0)
    payload += kernel.ljust(align(len(kernel)), b'\0') + ramdisk.ljust(align(len(ramdisk)), b'\0')
    raw = shim.ljust(payload_offset, b'\0') + payload
    if BASE + len(raw) >= 0x81F00000:
        raise ValueError("loaded image approaches the BootShim stack")
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    spec = importlib.util.spec_from_file_location('bootimg', ROOT / 'tools/mkbootimg-lilac.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    image = module.pack_bootimg(compressed, b'', dtb, cmdline='androidboot.hardware=qcom')
    if len(image) > 0x1000000:
        raise ValueError("image exceeds Sony's 16 MiB Fastboot buffer")
    (out / 'xnu-boot.img').write_bytes(image)
    (out / 'shim-with-kernel.bin.gz').write_bytes(compressed)
    shutil.copy2(a.kernel, out / 'hakios-kernel')
    ramdisk_name = 'hello' if a.ramdisk_format == 'macho' else 'ramdisk.apfs'
    if a.ramdisk.resolve() != out / ramdisk_name:
        shutil.copy2(a.ramdisk, out / ramdisk_name)
    inputs = [a.kernel, a.ramdisk, a.dtb, Path(__file__).resolve(), ROOT / 'tools/mkbootimg-lilac.py',
              *hello_inputs,
              *[ROOT / 'bootloader' / n for n in ('shim-main.c', 'shim-entry.S', 'shim.ld', 'boot_args.h', 'font_8x16.c', 'font_8x16.h')]]
    if a.smp:
        inputs.extend(ROOT / 'bootloader' / n for n in ('smp-topology.h', 'smp-return.h'))
    if a.usb_debug:
        inputs.extend(debug_sources)
        inputs.append(HAKIOS / 'tools/soc-args.py')
    if a.ufs_snapshot or a.ufs_read_probe or a.ufs_handoff_snapshot or a.smp:
        inputs.append(HAKIOS / 'tools/soc-args.py')
    if a.ufs_read_probe:
        inputs.extend(ROOT / 'bootloader' / n for n in ('ufs-read.c', 'ufs-read.h'))
    if a.ufs_handoff_snapshot:
        inputs.extend(ROOT / 'bootloader' / n for n in ('ufs-handoff.c', 'ufs-handoff.h'))
    manifest = {'kernel': km, 'hello': rm if a.ramdisk_format == 'macho' else None,
                'ramdisk': rm, 'ramdisk_format': a.ramdisk_format,
                'payload_offset': payload_offset, 'ramdisk_pa': hex(rd_pa),
                'ramdisk_bytes': len(ramdisk), 'ramdisk_padded_bytes': align(len(ramdisk)),
                'ramdisk_memory_bytes': rd_memory_bytes, 'ramdisk_vm_page_bytes': RAMDISK_VM_PAGE,
                'image_bytes': len(image), 'compile_flags': flags, 'hello_origin': hello_origin,
                'extra_boot_args': a.extra_boot_args,
                'quiet_boot': a.quiet_boot,
                'usb_debug': usb_debug,
                'smp':a.smp,
                'ufs_snapshot': ufs_diagnostic,
                'inputs': {str(f.resolve()): hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    files = sorted(f for f in out.iterdir() if f.is_file())
    (out / 'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest() + '  ' + f.name + '\n' for f in files))
    print(json.dumps({'image': str(out / 'xnu-boot.img'), **{k: manifest[k] for k in ('image_bytes', 'ramdisk_pa', 'ramdisk_bytes')}}))


if __name__ == '__main__':
    main()
