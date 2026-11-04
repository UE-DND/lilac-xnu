import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UserspaceOptionsTests(unittest.TestCase):
    def reject(self, options, message):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'image'
            command = [sys.executable, '-m', 'tools.lilaclib.userspace',
                       '--kernel', str(Path(directory) / 'unused-kernel'), '--output', str(output), *options]
            result = subprocess.run(command, cwd=ROOT, env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'},
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn(message, result.stderr)
            self.assertFalse(output.exists())

    def test_return_deadline_is_bounded(self):
        for value in ('-1', '3601'):
            with self.subTest(value=value):
                self.reject(['--return-after', value], '--return-after must be between 0 and 3600')

    def test_debug_smp_reaches_other_option_validation(self):
        self.reject(['--usb-debug', '--smp', '--touchscreen'], '--touchscreen requires --desktop-attempt')

    def test_touch_requires_desktop(self):
        self.reject(['--touchscreen'], '--touchscreen requires --desktop-attempt')

    def test_buttons_require_hid_bridge(self):
        self.reject(['--buttons'], '--buttons requires --touchscreen')

    def test_desktop_bringup_requires_interactive_input(self):
        for options in ([], ['--fast-boot'], ['--touchscreen']):
            self.reject(['--desktop-bringup', *options], '--desktop-bringup requires --fast-boot --touchscreen')

    def test_ram_migration_state_requires_desktop(self):
        self.reject(['--ram-migration-state'], '--ram-migration-state requires --desktop-attempt')

    def test_gpu_mesa_requires_desktop_firmware_and_touch(self):
        self.reject(['--gpu-mesa-dir', '/unused'],
                    '--gpu-mesa-dir requires --fast-boot --touchscreen and --gpu-firmware-dir')


if __name__ == '__main__':
    unittest.main()
