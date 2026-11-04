import json
import os
from pathlib import Path
import re
import subprocess
import time
import zlib

from .paths import OUT
from .records import digest, run_directory, save_json


class Fastboot:
    def __init__(self, executable, transcript):
        self.executable = str(executable)
        self.transcript = transcript

    def command(self, serial, *args, timeout=15):
        command = [self.executable, *(['-s', serial] if serial else []), *args]
        with self.transcript.open('a') as log:
            log.write(json.dumps(command) + '\n')
            log.flush()
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            except subprocess.TimeoutExpired as error:
                for partial in (error.stdout, error.stderr):
                    if partial:
                        log.write(partial.decode(errors='replace') if isinstance(partial, bytes) else partial)
                log.write(f'\nHOST TIMEOUT after {timeout}s; device outcome unknown\n')
                raise
            log.write(result.stdout + result.stderr)
        if result.returncode:
            raise ValueError(f'Fastboot failed: {args}: {result.stdout}{result.stderr}')
        return result.stdout + result.stderr

    def devices(self):
        return [line.split()[0] for line in self.command(None, 'devices').splitlines()
                if len(line.split()) == 2 and line.split()[1] == 'fastboot']

    def variable(self, serial, name):
        text = self.command(serial, 'getvar', name)
        values = re.findall(r'^(?:\(bootloader\) )?' + re.escape(name) + r':\s*(.*?)\s*$', text, re.M)
        if len(values) != 1:
            raise ValueError('missing or ambiguous Fastboot variable: ' + name)
        return values[0]


def wait_device(fastboot, serial, timeout, reboot_requested=False):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        devices = fastboot.devices()
        if devices.count(serial) > 1:
            raise ValueError('multiple devices have the requested serial')
        if serial in devices:
            return
        time.sleep(1)
    context = 'reboot requested, saved log retained; Fastboot return unverified' if reboot_requested else \
        'no reset requested, RAM log may still be on device'
    raise ValueError(f'timed out waiting for {serial}; {context}')


def sync_directory(directory):
    descriptor = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def save_receipt(path, receipt):
    save_json(path, receipt)
    with path.open('rb') as stream:
        os.fsync(stream.fileno())
    sync_directory(path.parent)


def verify_sony(fastboot, serial):
    identity = {name: fastboot.variable(serial, name) for name in ('product', 'version-bootloader', 'unlocked')}
    if identity != {'product': 'SO-02K', 'version-bootloader': '1306-5035_X_Boot_MSM8998_LA2.0_P_114', 'unlocked': 'yes'}:
        raise ValueError('refusing unexpected or locked device: ' + repr(identity))
    return identity


def collect(fastboot, manifest, output, sony_serial, timeout, reboot):
    debug = manifest.get('usb_debug')
    if not debug or not re.fullmatch('[0-9a-f]{32}', debug.get('run_id', '')):
        raise ValueError('build does not contain a versioned USB debug service')
    serial = 'lilac-debug'
    wait_device(fastboot, serial, timeout)
    if fastboot.variable(serial, 'product') != 'lilac-ram-debug':
        raise ValueError('unexpected USB debug product')
    if fastboot.variable(serial, 'run-id') != debug['run_id']:
        raise ValueError('device run-id does not match this build; refusing acknowledgement or reboot')
    length_text = fastboot.variable(serial, 'log-size')
    crc_text = fastboot.variable(serial, 'log-crc32')
    if not re.fullmatch('[0-9a-f]{8}', length_text) or not re.fullmatch('[0-9a-f]{8}', crc_text):
        raise ValueError('invalid log metadata')
    length, crc = int(length_text, 16), int(crc_text, 16)
    if not 0 < length <= 2 * 1024 * 1024:
        raise ValueError('log length out of bounds')
    temporary = output / 'kernel.log.partial'
    fastboot.command(serial, 'get_staged', str(temporary), timeout=60)
    data = temporary.read_bytes()
    if len(data) != length or zlib.crc32(data) != crc:
        raise ValueError('log length/CRC mismatch; preserving partial log and refusing reboot')
    if debug.get('self_test') and data != b'LILAC USB SELF TEST\nRAM only; no XNU, no persistent storage writes.\n':
        raise ValueError('USB self-test payload mismatch')
    with temporary.open('rb') as stream:
        os.fsync(stream.fileno())
    destination = output / 'kernel.log'
    temporary.rename(destination)
    sync_directory(output)
    receipt = {'status': 'log_saved', 'run_id': debug['run_id'], 'bytes': length,
               'crc32': crc_text, 'sha256': digest(destination), 'bootloader_returned': False,
               'reboot_requested': False}
    save_receipt(output / 'receipt.json', receipt)
    if reboot:
        fastboot.command(serial, 'oem', 'log-ack:' + crc_text)
        receipt.update(status='log_saved_reboot_requested', reboot_requested=True)
        try:
            fastboot.command(serial, 'reboot-bootloader')
        finally:
            save_receipt(output / 'receipt.json', receipt)
        wait_device(fastboot, sony_serial, timeout, reboot_requested=True)
        receipt['identity_after'] = verify_sony(fastboot, sony_serial)
        receipt['status'] = 'log_saved_fastboot_verified'
        receipt['bootloader_returned'] = True
        save_receipt(output / 'receipt.json', receipt)
    return receipt


def run(args):
    build = args.build.resolve()
    if (build / 'image/manifest.json').is_file():
        build /= 'image'
    manifest = json.loads((build / 'manifest.json').read_text())
    if not manifest.get('usb_debug'):
        raise ValueError('rebuild with --usb-debug or --usb-self-test first')
    output = run_directory('device-' + args.action)
    output.mkdir(parents=True, exist_ok=False)
    fastboot = Fastboot(args.fastboot, output / 'fastboot.log')
    record = {'status': 'started', 'build': str(build), 'serial': args.serial,
              'firmware_updates': False, 'partition_writes': False}
    save_json(output / 'result.json', record)
    print('Device report: ' + str(output), flush=True)
    try:
        if args.action == 'run':
            wait_device(fastboot, args.serial, args.timeout)
            record['identity_before'] = verify_sony(fastboot, args.serial)
            image = build / 'xnu-boot.img'
            expected = dict(line.split('  ', 1)[::-1] for line in (build / 'SHA256SUMS').read_text().splitlines())
            if digest(image) != expected.get('xnu-boot.img'):
                raise ValueError('RAM boot image does not match SHA256SUMS')
            record['image_sha256'] = digest(image)
            tool = OUT / 'latest/xperable/xperable'
            tool_manifest = json.loads(tool.with_name('manifest.json').read_text())
            if tool_manifest.get('readonly_survey') is not False or digest(tool) != tool_manifest.get('binary_sha256'):
                raise ValueError('xperable must be a verified normal p114 build')
            command = [str(tool.resolve()), '-v', '-V', '-I', str(image), '-1', '-c', 'boot', '-1']
            record['boot_command'] = command
            record['xperable_sha256'] = digest(tool)
            if fastboot.devices() != [args.serial]:
                raise ValueError('xperable requires exactly one connected Fastboot device')
            save_json(output / 'result.json', record)
            with (output / 'ramboot.log').open('w') as log:
                try:
                    result = subprocess.run(command, cwd=tool.resolve().parent, stdout=log,
                                            stderr=subprocess.STDOUT, timeout=60)
                    record['boot_command_exit'] = result.returncode
                except subprocess.TimeoutExpired:
                    record['boot_command_exit'] = 'host timeout; device outcome unknown'
        receipt = collect(fastboot, manifest, output, args.serial, args.timeout, args.reboot_bootloader)
        record.update(receipt)
        print(receipt['status'], flush=True)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if (output / 'receipt.json').is_file():
            record.update(json.loads((output / 'receipt.json').read_text()))
        record.update(status='failed', error=str(error))
        raise ValueError(str(error) + '; report: ' + str(output)) from error
    finally:
        save_json(output / 'result.json', record)
