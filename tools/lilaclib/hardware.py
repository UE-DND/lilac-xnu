import argparse
import importlib.util
import struct
import subprocess
import sys
import shutil
from pathlib import Path

from .paths import ROOT, HAKIOS, DEPS, local_path
from .records import digest, save_json


def validate_dtb(blob, gpu_cx=False):
    spec = importlib.util.spec_from_file_location('hardware_soc', HAKIOS / 'tools/soc-args.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    nodes = module.nodes_of(blob)
    validate_resources(nodes)
    if gpu_cx:
        validate_gpu_cx(nodes)
    return nodes


def validate_buttons(nodes):
    base = '/soc/qcom,spmi@800f000'
    pm = base + '/qcom,pm8998@0'
    gpio = nodes.get(pm + '/gpios', {})
    phandle = gpio.get('phandle', b'')
    cells = lambda *v: struct.pack('>' + 'I' * len(v), *v)
    expected = {
        (base, 'reg'): cells(0x800f000, 0x1000, 0x8400000, 0x1000000, 0x9400000, 0x1000000, 0xa400000, 0x220000, 0x800a000, 0x3000),
        (base, 'qcom,ee'): cells(0),
        (pm, 'reg'): cells(0, 0),
        (pm + '/qcom,power-on@800', 'reg'): cells(0x800, 0x100),
        (pm + '/qcom,power-on@800/qcom,pon_1', 'qcom,pon-type'): cells(0),
        (pm + '/qcom,power-on@800/qcom,pon_1', 'linux,code'): cells(116),
        (pm + '/gpios/gpio@c500', 'reg'): cells(0xc500, 0x100),
        (pm + '/gpios/gpio@c600', 'reg'): cells(0xc600, 0x100),
        ('/soc/gpio_keys/vol_up', 'gpios'): phandle + cells(6, 1),
        ('/soc/gpio_keys/vol_dn', 'gpios'): phandle + cells(5, 1),
    }
    if len(phandle) != 4 or any(nodes.get(path, {}).get(key) != value or
        nodes.get(path, {}).get('status', b'okay\0') not in (b'ok\0', b'okay\0')
        for (path, key), value in expected.items()):
        raise ValueError('buttons require the verified PM8998 PON and GPIO key mapping')


def validate_gpu_cx(nodes):
    gpu = nodes.get('/soc/qcom,kgsl-3d0@5000000', {})
    cx = nodes.get('/soc/qcom,gdsc@5066004', {})
    smmu = nodes.get('/soc/arm,smmu-kgsl@5040000', {})
    user = nodes.get('/soc/qcom,kgsl-iommu/gfx3d_user', {})
    if (gpu.get('reg') != struct.pack('>II', 0x05000000, 0x40000) or
        cx.get('compatible') != b'qcom,gdsc\0' or
        cx.get('reg') != struct.pack('>IIII', 0x05066004, 4, 0x05066008, 4) or
        cx.get('reg-names') != b'base\0hw_ctrl_addr\0' or
        cx.get('status', b'okay\0') not in (b'ok\0', b'okay\0') or
        len(cx.get('phandle', b'')) != 4 or gpu.get('vddcx-supply') != cx['phandle'] or
        smmu.get('compatible') != b'qcom,smmu-v2\0' or
        smmu.get('reg') != struct.pack('>II', 0x05040000, 0x10000) or
        smmu.get('status', b'okay\0') not in (b'ok\0', b'okay\0') or
        len(smmu.get('phandle', b'')) != 4 or
        user.get('iommus') != smmu['phandle'] + struct.pack('>I', 0)):
        raise ValueError('GPU CX cycle requires verified GPU and CX supply resources')


def validate_gpu_deep(nodes):
    rpm = nodes.get('/soc/qcom,glink-smem-native-xprt-rpm@778000', {})
    if (rpm.get('compatible') != b'qcom,glink-rpm-native-xprt\0' or
        rpm.get('reg') != struct.pack('>IIII', 0x778000, 0x7000, 0x17911008, 4) or
        rpm.get('reg-names') != b'msgram\0irq-reg-base\0' or
        rpm.get('qcom,irq-mask') != struct.pack('>I', 1) or
        rpm.get('status', b'okay\0') not in (b'ok\0', b'okay\0')):
        raise ValueError('GPU deep probe requires verified RPM GLINK resources')


def validate_gpu_rpm(nodes):
    validate_gpu_deep(nodes)
    client = nodes.get('/soc/qcom,rpm-smd', {})
    resource = nodes.get('/soc/qcom,rpm-smd/rpm-regulator-smpa9', {})
    level = nodes.get('/soc/qcom,rpm-smd/rpm-regulator-smpa9/regulator-s9-level', {})
    gfx = nodes.get('/soc/qcom,gfxcc@5065000', {})
    cell = lambda value: struct.pack('>I', value)
    if (client.get('compatible') != b'qcom,rpm-glink\0' or
        client.get('qcom,glink-edge') != b'rpm\0' or
        client.get('rpm-channel-name') != b'rpm_requests\0' or
        'reg' in client or
        resource.get('compatible') != b'qcom,rpm-smd-regulator-resource\0' or
        resource.get('qcom,resource-name') != b'rwmx\0' or
        resource.get('qcom,resource-id') != cell(0) or
        level.get('compatible') != b'qcom,rpm-smd-regulator\0' or
        level.get('qcom,set') != cell(3) or level.get('qcom,use-voltage-level') != b'' or
        level.get('regulator-min-microvolt') != cell(0x10) or
        level.get('regulator-max-microvolt') != cell(0x180) or
        len(level.get('phandle', b'')) != 4 or
        gfx.get('compatible') != b'qcom,gfxcc-8998-v2\0' or
        gfx.get('vdd_mx-supply') != level['phandle'] or
        gfx.get('vdd_gpu_mx-supply') != level['phandle'] or
        any(node.get('status', b'okay\0') not in (b'ok\0', b'okay\0')
            for node in (client, resource, level, gfx))):
        raise ValueError('GPU RPM vote requires verified version-0 rwmx voltage-level resource and GPU supply')


def validate_gpu_backend(nodes):
    validate_gpu_cx(nodes)
    expected = {
        ('/soc/qcom,spmi@800f000', 'compatible'): b'qcom,spmi-pmic-arb\0',
        ('/soc/qcom,spmi@800f000', 'reg'): struct.pack('>10I', 0x800f000, 0x1000,
            0x8400000, 0x1000000, 0x9400000, 0x1000000, 0xa400000, 0x220000, 0x800a000, 0x3000),
        ('/soc/qcom,spmi@800f000', 'reg-names'): b'core\0chnls\0obsrvr\0intr\0cnfg\0',
        ('/soc/qcom,spmi@800f000', 'qcom,ee'): struct.pack('>I', 0),
        ('/soc/qcom,spmi@800f000/qcom,pm8005@5', 'compatible'): b'qcom,spmi-pmic\0',
        ('/soc/qcom,spmi@800f000/qcom,pm8005@5', 'reg'): struct.pack('>II', 5, 0),
        ('/soc/qcom,spmi@800f000/qcom,pm8005@5/regulator@1400', 'compatible'): b'qcom,qpnp-regulator\0',
        ('/soc/qcom,spmi@800f000/qcom,pm8005@5/regulator@1400', 'reg'): struct.pack('>II', 0x1400, 0x100),
        ('/soc/cpr4-ctrl@5061000', 'compatible'): b'qcom,cpr4-msm8998-v2-mmss-regulator\0',
        ('/soc/cpr4-ctrl@5061000', 'reg'): struct.pack('>6I', 0x5061000, 0x4000, 0x784000, 0x1000, 0x5065204, 4),
        ('/soc/regulator@1fcf004', 'compatible'): b'qcom,mem-acc-regulator\0',
        ('/soc/regulator@1fcf004', 'reg'): struct.pack('>II', 0x1fcf004, 4),
    }
    for (path, key), value in expected.items():
        node = nodes.get(path, {})
        if node.get(key) != value or node.get('status', b'okay\0') not in (b'ok\0', b'okay\0'):
            raise ValueError(f'A540 backend requires verified resource: {path}/{key}')


def validate_gpu_power(nodes):
    validate_gpu_backend(nodes)
    validate_gpu_rpm(nodes)
    gx = nodes.get('/soc/qcom,gdsc@5066094', {})
    gpu = nodes.get('/soc/qcom,kgsl-3d0@5000000', {})
    cpr = nodes.get('/soc/cpr4-ctrl@5061000', {})
    corner = nodes.get('/soc/cpr4-ctrl@5061000/thread@0/regulator', {})
    pm = nodes.get('/soc/qcom,spmi@800f000/qcom,pm8005@5/regulator@1400', {})
    acc = nodes.get('/soc/regulator@1fcf004', {})
    cells = lambda *values: struct.pack('>' + 'I' * len(values), *values)
    for provider, consumer, supply in ((gx, gpu, 'vdd-supply'), (corner, gx, 'parent-supply'),
                                       (pm, cpr, 'vdd-supply'), (acc, cpr, 'mem-acc-supply')):
        if len(provider.get('phandle', b'')) != 4 or consumer.get(supply) != provider['phandle']:
            raise ValueError('A540 power requires verified GX/CPR4/PM8005/memacc supply chain')
    ceilings = corner.get('qcom,cpr-voltage-ceiling', b'')
    if (gx.get('compatible') != b'qcom,gdsc\0' or gx.get('reg') != cells(0x5066094, 4, 0x5065130, 4, 0x5066090, 4) or
        gx.get('reg-names') != b'base\0domain_addr\0sw_reset\0' or
        pm.get('regulator-min-microvolt') != cells(516000) or pm.get('regulator-max-microvolt') != cells(1088000) or
        corner.get('qcom,corner-frequencies', b'')[:4] != cells(180000000) or
        len(ceilings) != 256 or any(ceilings[32*i:32*i+4] != cells(716000 if i == 0 else 724000) for i in range(8)) or
        acc.get('qcom,corner-acc-map') != cells(1, 0) or acc.get('qcom,acc-sel-l1-bit-pos') != cells(0) or
        acc.get('qcom,acc-sel-l1-bit-size') != cells(1) or
        any(node.get('status', b'okay\0') not in (b'ok\0', b'okay\0') for node in (gx, corner, pm, acc))):
        raise ValueError('A540 power requires verified GX sequence and low-corner voltage/timing limits')


def validate_gpu_dma(nodes):
    validate_gpu_power(nodes)
    gpu = nodes['/soc/qcom,kgsl-3d0@5000000']
    gcc = nodes['/soc/qcom,gcc@100000']['phandle']
    gfx = nodes['/soc/qcom,gfxcc@5065000']['phandle']
    gpucc = nodes['/soc/qcom,gpucc@5065000']['phandle']
    providers = (gfx, gcc, gpucc, gcc, gcc, gpucc, gpucc, gcc)
    ids = (0x95f01bd5, 0x72f20a57, 0x58a0a7ca, 0x3edd69ad, 0x3909459b, 0xb2678e80, 0x7bd750e8, 0xfd82abad)
    if (gpu.get('clock-names') != b'core_clk\0iface_clk\0rbbmtimer_clk\0mem_clk\0mem_iface_clk\0isense_clk\0rbcpr_clk\0iref_clk\0' or
        gpu.get('clocks') != b''.join(handle + struct.pack('>I', ident) for handle, ident in zip(providers, ids))):
        raise ValueError('A540 DMA requires verified GPU BIMC clock bindings')


def validate_gpu_zap(nodes):
    firmware = nodes.get('/soc/qcom,kgsl-hyp', {})
    gpu = nodes.get('/soc/qcom,kgsl-3d0@5000000', {})
    if (firmware.get('compatible') != b'qcom,pil-tz-generic\0' or
        firmware.get('qcom,pas-id') != struct.pack('>I', 13) or
        firmware.get('qcom,firmware-name') != b'a540_zap\0' or
        firmware.get('status', b'okay\0') not in (b'ok\0', b'okay\0') or
        'memory-region' in firmware or gpu.get('qcom,zap-shader') != b'a540_zap\0'):
        raise ValueError('A540 ZAP requires verified PAS image 13 and dynamically allocated firmware memory')


def validate_gpu_render(nodes):
    validate_gpu_dma(nodes)
    validate_gpu_zap(nodes)
    gpu = nodes['/soc/qcom,kgsl-3d0@5000000']
    if (gpu.get('qcom,chipid') != struct.pack('>I', 0x05040001) or
        gpu.get('qcom,highest-bank-bit') != struct.pack('>I', 15) or
        gpu.get('qcom,gpu-quirk-lmloadkill-disable') != b''):
        raise ValueError('A540 rendering requires the verified chip, UBWC bank and LMLOADKILL quirk')


def validate_resources(nodes):
    expected = {
        ('', '#address-cells'): struct.pack('>I', 2),
        ('/soc', '#address-cells'): struct.pack('>I', 1),
        ('/soc', '#size-cells'): struct.pack('>I', 1),
        ('/soc', 'ranges'): struct.pack('>IIII', 0, 0, 0, 0xffffffff),
        ('/soc/i2c@c179000', 'compatible'): b'qcom,i2c-msm-v2\0',
        ('/soc/i2c@c179000', 'reg'): struct.pack('>II', 0x0c179000, 0x600),
        ('/soc/i2c@c179000', 'qcom,clk-freq-in'): struct.pack('>I', 19200000),
        ('/soc/i2c@c179000', 'clock-names'): b'iface_clk\0core_clk\0',
        ('/soc/qcom,gcc@100000', 'compatible'): b'qcom,gcc-8998-v2\0',
        ('/soc/qcom,gcc@100000', 'reg'): struct.pack('>II', 0x100000, 0xb0000),
        ('/soc/qcom,gcc@100000', '#clock-cells'): struct.pack('>I', 1),
        ('/soc/i2c@c179000/synaptics_clearpad@2c', 'reg'): struct.pack('>I', 0x2c),
        ('/soc/i2c@c179000/synaptics_clearpad@2c', 'compatible'): b'synaptics,clearpad\0',
        ('/soc/i2c@c179000/synaptics_clearpad@2c', 'interrupts'): struct.pack('>II', 125, 2),
        ('/soc/qcom,kgsl-3d0@5000000', 'qcom,chipid'): struct.pack('>I', 0x05040001),
        ('/soc/qcom,gpucc@5065000', 'reg'): struct.pack('>II', 0x05065000, 0x9000),
        ('/soc/pinctrl@03400000', 'compatible'): b'qcom,msm8998-pinctrl\0',
        ('/soc/pinctrl@03400000', 'reg'): struct.pack('>II', 0x03400000, 0xc00000),
        ('/soc/pinctrl@03400000', '#gpio-cells'): struct.pack('>I', 2),
        ('/soc/pinctrl@03400000', 'gpio-controller'): b'',
        ('/soc/pinctrl@03400000/msm_gpio_87/mux', 'function'): b'blsp_i2c5\0',
        ('/soc/pinctrl@03400000/msm_gpio_88/mux', 'function'): b'blsp_i2c5\0',
        ('/soc/pinctrl@03400000/msm_gpio_87/mux', 'pins'): b'gpio87\0',
        ('/soc/pinctrl@03400000/msm_gpio_88/mux', 'pins'): b'gpio88\0',
    }
    for (node, key), value in expected.items():
        if nodes.get(node, {}).get(key) != value:
            raise ValueError(f'hardware probe requires verified Lilac DTB: {node}/{key}')
    for node in ('/soc/i2c@c179000', '/soc/qcom,kgsl-3d0@5000000', '/soc/qcom,gcc@100000',
                 '/soc/pinctrl@03400000'):
        if nodes[node].get('status', b'okay\0') not in (b'ok\0', b'okay\0'):
            raise ValueError(f'hardware probe refuses disabled node: {node}')
    handles = [nodes.get('/soc/pinctrl@03400000/msm_gpio_' + str(pin), {}).get('phandle')
               for pin in (87, 88)]
    if any(handle is None or len(handle) != 4 for handle in handles) or handles[0] == handles[1] or \
            nodes['/soc/i2c@c179000'].get('pinctrl-0') != b''.join(handles):
        raise ValueError('hardware probe refuses unverified I2C pinctrl mapping')
    gcc = nodes['/soc/qcom,gcc@100000'].get('phandle')
    if gcc is None or len(gcc) != 4 or nodes['/soc/i2c@c179000'].get('clocks') != \
            gcc + struct.pack('>I', 0x8caa5b4f) + gcc + struct.pack('>I', 0xacae5604):
        raise ValueError('hardware probe requires BLSP1 AHB and QUP5 I2C clock IDs from the Sony binding')
    gpio = nodes['/soc/pinctrl@03400000'].get('phandle')
    display = nodes.get('/soc/qcom,mdss_dsi@0/qcom,mdss_dsi_ctrl0@c994000', {})
    for name, pin in (('reset', 89), ('int', 125), ('vddio', 133)):
        if gpio is None or len(gpio) != 4 or display.get('qcom,platform-touch-' + name + '-gpio') != \
                gpio + struct.pack('>II', pin, 0):
            raise ValueError('hardware probe refuses unverified touch ' + name + ' GPIO')


def main():
    parser = argparse.ArgumentParser(prog='./lilac debug hardware', description='Build an opt-in touch acquisition / GPU clock snapshot RAM probe, not a desktop image')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--kernel', required=True, type=Path)
    parser.add_argument('--dtb', type=Path, default=local_path('dtb'))
    parser.add_argument('--touch', action='store_true', help='enable QUP I2C and S3330 wake/read; no controller firmware writes')
    parser.add_argument('--gpu-snapshot', action='store_true', help='read GPUCC power/clock registers only; no GPU submission')
    parser.add_argument('--gpu-cx-cycle', action='store_true', help='test and release A540 CX, read guarded SMMU IDs and query TrustZone; requires timed USB debug')
    parser.add_argument('--gpu-deep-snapshot', action='store_true', help='also read RPM GLINK layout and SMMU context 0; requires --gpu-cx-cycle')
    parser.add_argument('--gpu-rpm-cycle', action='store_true', help='connect RPM, request MX level 0x80 and release after ACK; requires --gpu-cx-cycle')
    parser.add_argument('--gpu-backend-stage', type=int, choices=(1, 2, 3, 4, 5, 6, 7), default=0,
                        help='1: PM8005; 2: SMMU; 3: GX; 4: CP/DMA; 5: ZAP; 6: render setup and IB/fence; 7: keep Mesa service; requires --gpu-cx-cycle')
    parser.add_argument('--gpu-firmware-dir', type=Path, help='local A540 firmware; required for backend stage 4')
    parser.add_argument('--usb-debug', action='store_true', help='export logs after kernel return')
    parser.add_argument('--return-after', type=int, default=0, help='return to BootShim after this many seconds of kernel runtime')
    args = parser.parse_args()
    if not args.touch and not args.gpu_snapshot:
        parser.error('select --touch and/or --gpu-snapshot explicitly')
    if args.gpu_cx_cycle and (not args.gpu_snapshot or not args.usb_debug or not args.return_after):
        parser.error('--gpu-cx-cycle requires --gpu-snapshot --usb-debug and --return-after')
    if args.gpu_deep_snapshot and not args.gpu_cx_cycle:
        parser.error('--gpu-deep-snapshot requires --gpu-cx-cycle')
    if args.gpu_rpm_cycle and not args.gpu_cx_cycle:
        parser.error('--gpu-rpm-cycle requires --gpu-cx-cycle')
    if args.gpu_backend_stage and not args.gpu_cx_cycle:
        parser.error('--gpu-backend-stage requires --gpu-cx-cycle')
    if args.gpu_backend_stage >= 4 and not args.gpu_firmware_dir:
        parser.error('--gpu-backend-stage 4 requires --gpu-firmware-dir')
    if args.return_after < 0 or args.return_after > 3600:
        parser.error('--return-after must be between 0 and 3600')
    validate_dtb(args.dtb.read_bytes(), gpu_cx=args.gpu_cx_cycle)
    if args.gpu_deep_snapshot or args.gpu_rpm_cycle or args.gpu_backend_stage:
        spec = importlib.util.spec_from_file_location('hardware_soc', HAKIOS / 'tools/soc-args.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        validate_gpu_deep(module.nodes_of(args.dtb.read_bytes()))
        if args.gpu_rpm_cycle:
            validate_gpu_rpm(module.nodes_of(args.dtb.read_bytes()))
        if args.gpu_backend_stage:
            validate_gpu_backend(module.nodes_of(args.dtb.read_bytes()))
        if args.gpu_backend_stage >= 3:
            validate_gpu_power(module.nodes_of(args.dtb.read_bytes()))
        if args.gpu_backend_stage >= 4:
            validate_gpu_dma(module.nodes_of(args.dtb.read_bytes()))
        if args.gpu_backend_stage >= 5:
            validate_gpu_zap(module.nodes_of(args.dtb.read_bytes()))
        if args.gpu_backend_stage >= 6:
            validate_gpu_render(module.nodes_of(args.dtb.read_bytes()))
    firmware_report = None
    if args.gpu_firmware_dir:
        from tools import a540_firmware
        firmware_report = a540_firmware.validate(args.gpu_firmware_dir)
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    program = output / 'hardware-probe'
    command = ['bash', '-c', 'set -eu; source "$1"; shift; exec xnu-clang "$@"',
               'lilac-hardware', str(HAKIOS / 'xnu-env.sh'),
               '-arch', 'arm64', '-march=armv8-a', '-Os', '-static', '-nostdlib', '-ffreestanding',
               '-fno-stack-protector', '-fno-builtin', '-mstrict-align', '-mgeneral-regs-only',
               '-I' + str(HAKIOS / 'userland/libmini'), '-Wl,-e,_start', '-Wl,-pagezero_size,0x100000000',
               str(ROOT / 'userland/hardware-probe.c'), str(HAKIOS / 'userland/libmini/mini.c'),
               str(HAKIOS / 'userland/libmini/start.S'), '-o', str(program)]
    with (output / 'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        payload, payload_format = program, 'macho'
        if firmware_report:
            root = output / 'root'
            (root / 'sbin').mkdir(parents=True)
            (root / 'fw').mkdir()
            (root / 'dev').mkdir()
            shutil.copy2(program, root / 'sbin/launchd')
            for name, expected in firmware_report['files'].items():
                target = root / 'fw' / name
                shutil.copyfile(args.gpu_firmware_dir / name, target)
                if digest(target) != expected['sha256']:
                    raise ValueError(f'A540 firmware changed during build: {name}')
            payload, payload_format = output / 'root.apfs', 'apfs'
            subprocess.run([sys.executable, str(HAKIOS / 'tools/apfs/mkapfs.py'), '--plain', 'fw',
                            '-o', str(payload), 'LilacHardware=' + str(root)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            for path in (root / 'sbin/launchd', *sorted((root / 'fw').iterdir())):
                readback = subprocess.check_output([str(DEPS / 'host/bin/apfs-tool'), 'cat',
                                                     str(payload), '/' + str(path.relative_to(root))])
                if readback != path.read_bytes():
                    raise ValueError(f'hardware APFS readback differs: {path.name}')
        boot_args = (['lilac_touch=1'] if args.touch else []) + (['lilac_gpu_snapshot=1'] if args.gpu_snapshot else [])
        if args.gpu_cx_cycle:
            boot_args.append('lilac_gpu_cx=1')
        if args.gpu_deep_snapshot:
            boot_args.append('lilac_gpu_deep=1')
        if args.gpu_rpm_cycle:
            boot_args.append('lilac_gpu_rpm=1')
        if args.gpu_backend_stage:
            boot_args.append('lilac_a540=' + str(args.gpu_backend_stage))
        if args.return_after:
            boot_args.append('loader_return_after=' + str(args.return_after))
        subprocess.run([sys.executable, '-m', 'tools.lilaclib.ramboot', '--kernel', str(args.kernel.resolve()),
                        '--dtb', str(args.dtb.resolve()), '--ramdisk', str(payload), '--ramdisk-format', payload_format,
                        '--extra-boot-args', ' '.join(boot_args), '--output', str(output / 'image'),
                        *(['--usb-debug'] if args.usb_debug else [])],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    image = output / 'image/xnu-boot.img'
    save_json(output / 'manifest.json', {'purpose': 'hardware acquisition probe, not GPU acceleration or desktop acceptance',
              'touch': args.touch, 'gpu_snapshot': args.gpu_snapshot, 'gpu_cx_cycle': args.gpu_cx_cycle,
              'gpu_deep_snapshot': args.gpu_deep_snapshot, 'storage_writes': False,
              'gpu_rpm_cycle': args.gpu_rpm_cycle,
              'gpu_backend_stage': args.gpu_backend_stage,
              'gpu_firmware': firmware_report,
              'firmware_updates': False, 'image_sha256': digest(image), 'image_bytes': image.stat().st_size,
              'kernel_sha256': digest(args.kernel), 'dtb_sha256': digest(args.dtb), 'compile_command': command})


if __name__ == '__main__':
    main()
