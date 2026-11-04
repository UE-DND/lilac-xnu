from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lilaclib import device


class FakeFastboot:
    def __init__(self):
        self.calls = []
        self.data = b'kernel boot\npanic: test\n'
        self.metadata = {'product': 'lilac-ram-debug', 'run-id': '0' * 32,
                         'log-size': f'{len(self.data):08x}', 'log-crc32': f'{zlib.crc32(self.data):08x}'}
        self.returned = False

    def devices(self):
        return ['sony'] if self.returned else ['lilac-debug']

    def variable(self, serial, name):
        if serial == 'sony':
            return {'product': 'SO-02K', 'version-bootloader': '1306-5035_X_Boot_MSM8998_LA2.0_P_114', 'unlocked': 'yes'}[name]
        return self.metadata[name]

    def command(self, serial, *args, **kwargs):
        self.calls.append(args)
        if args[0] == 'get_staged':
            Path(args[1]).write_bytes(self.data)
        if args[0] == 'reboot-bootloader':
            self.returned = True
        return 'OKAY'


class DeviceTests(unittest.TestCase):
    def test_closed_loop(self):
        fake = FakeFastboot()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = device.collect(fake, {'usb_debug': {'run_id': '0' * 32}}, output, 'sony', 1, True)
            self.assertTrue(result['bootloader_returned'])
            self.assertEqual((output / 'kernel.log').read_bytes(), fake.data)
            self.assertEqual(fake.calls[-1], ('reboot-bootloader',))
            self.assertEqual(fake.calls[-2], ('oem', 'log-ack:' + fake.metadata['log-crc32']))

    def test_mismatch_never_reboots(self):
        for kind in ('product', 'run-id', 'log-size', 'log-crc32', 'corrupt', 'empty', 'large'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as directory:
                fake = FakeFastboot()
                if kind == 'corrupt': fake.data += b'corrupt'
                elif kind == 'empty': fake.metadata['log-size'] = '00000000'
                elif kind == 'large': fake.metadata['log-size'] = 'ffffffff'
                else: fake.metadata[kind] = 'bad'
                with self.assertRaises(ValueError):
                    device.collect(fake, {'usb_debug': {'run_id': '0' * 32}}, Path(directory), 'sony', 1, True)
                self.assertFalse(any(call[0] in ('oem', 'reboot-bootloader') for call in fake.calls))

    def test_fsync_failure_never_acknowledges(self):
        fake = FakeFastboot()
        with tempfile.TemporaryDirectory() as directory, patch.object(device.os, 'fsync', side_effect=OSError('disk error')):
            with self.assertRaises(OSError):
                device.collect(fake, {'usb_debug': {'run_id': '0' * 32}}, Path(directory), 'sony', 1, True)
        self.assertFalse(any(call[0] in ('oem', 'reboot-bootloader') for call in fake.calls))

    def test_collect_only(self):
        fake = FakeFastboot()
        with tempfile.TemporaryDirectory() as directory:
            result = device.collect(fake, {'usb_debug': {'run_id': '0' * 32}}, Path(directory), 'sony', 1, False)
            self.assertFalse(result['bootloader_returned'])
            self.assertEqual(len(fake.calls), 1)

    def test_reboot_timeout_keeps_durable_log(self):
        fake = FakeFastboot()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(device, 'wait_device', side_effect=[None, ValueError('return unverified')]):
                with self.assertRaisesRegex(ValueError, 'return unverified'):
                    device.collect(fake, {'usb_debug': {'run_id': '0' * 32}}, output, 'sony', 1, True)
            receipt = json.loads((output / 'receipt.json').read_text())
            self.assertEqual(receipt['status'], 'log_saved_reboot_requested')
            self.assertTrue(receipt['reboot_requested'])
            self.assertFalse(receipt['bootloader_returned'])
            self.assertEqual((output / 'kernel.log').read_bytes(), fake.data)

    def test_receipt_failure_never_acknowledges(self):
        fake = FakeFastboot()
        with tempfile.TemporaryDirectory() as directory, patch.object(device, 'save_json', side_effect=OSError('disk error')):
            with self.assertRaises(OSError):
                device.collect(fake, {'usb_debug': {'run_id': '0' * 32}}, Path(directory), 'sony', 1, True)
        self.assertFalse(any(call[0] in ('oem', 'reboot-bootloader') for call in fake.calls))

    def test_timeout_is_transcribed(self):
        with tempfile.TemporaryDirectory() as directory:
            transcript = Path(directory) / 'fastboot.log'
            fastboot = device.Fastboot('/test/fastboot', transcript)
            timeout = device.subprocess.TimeoutExpired(['fastboot'], 1, output=b'partial\n')
            with patch.object(device.subprocess, 'run', side_effect=timeout):
                with self.assertRaises(device.subprocess.TimeoutExpired):
                    fastboot.command('sony', 'getvar', 'product', timeout=1)
            self.assertIn('partial', transcript.read_text())
            self.assertIn('HOST TIMEOUT', transcript.read_text())

    def test_wait_timeout_distinguishes_reset(self):
        for requested in (False, True):
            with self.subTest(requested=requested), patch.object(device.time, 'monotonic', side_effect=[0, 2]):
                expected = 'reboot requested' if requested else 'no reset requested'
                with self.assertRaisesRegex(ValueError, expected):
                    device.wait_device(FakeFastboot(), 'sony', 1, reboot_requested=requested)


if __name__ == '__main__':
    unittest.main()
