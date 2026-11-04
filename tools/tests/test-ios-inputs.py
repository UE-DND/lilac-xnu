#!/usr/bin/env python3
"""Exercise input rejection and preservation with small synthetic IPSWs."""
import hashlib
import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lilaclib import ios as module


class InputsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.kernel = self.root / 'sysimage'
        self.dest = self.kernel
        self.dest.mkdir(parents=True)
        (self.dest / 'unpack.sh').write_text('# fixture\n')
        self.ipsw = self.root / 'fixture.ipsw'

    def make_archive(self, version='16.5'):
        with zipfile.ZipFile(self.ipsw, 'w') as archive:
            archive.writestr('BuildManifest.plist', plistlib.dumps({
                'ProductVersion': version, 'ProductBuildVersion': '20F66',
                'SupportedProductTypes': ['iPhone10,3', 'iPhone10,6']}))
            archive.writestr('system.dmg', b'fixture system image')
        self.addCleanup(patch.stopall)
        patch.object(module, 'PARTS', (('system.dmg', 'rootfs.dmg', 20),)).start()
        patch.object(module, 'IPSW_SIZE', self.ipsw.stat().st_size).start()
        patch.object(module, 'IPSW_SHA256', module.digest(self.ipsw)).start()

    def test_valid_input_and_repeat_preserve_bytes(self):
        self.make_archive()
        first = module.prepare(self.ipsw, self.kernel)
        target = self.dest / 'rootfs.dmg'
        original = target.stat()
        second = module.prepare(self.ipsw, self.kernel)
        self.assertEqual(first['images'], second['images'])
        self.assertEqual(target.read_bytes(), b'fixture system image')
        self.assertEqual(target.stat().st_mtime_ns, original.st_mtime_ns)
        self.assertEqual(first['images'][0]['sha256'], hashlib.sha256(target.read_bytes()).hexdigest())

    def test_bad_hash_writes_no_image(self):
        self.make_archive()
        with patch.object(module, 'IPSW_SHA256', '0' * 64):
            with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                module.prepare(self.ipsw, self.kernel)
        self.assertFalse((self.dest / 'rootfs.dmg').exists())

    def test_wrong_manifest_writes_no_image(self):
        self.make_archive(version='17.0')
        with self.assertRaisesRegex(ValueError, 'BuildManifest'):
            module.prepare(self.ipsw, self.kernel)
        self.assertFalse((self.dest / 'rootfs.dmg').exists())

    def test_wrong_member_size_writes_no_image(self):
        self.make_archive()
        with patch.object(module, 'PARTS', (('system.dmg', 'rootfs.dmg', 19),)):
            with self.assertRaisesRegex(ValueError, 'member size'):
                module.prepare(self.ipsw, self.kernel)
        self.assertFalse((self.dest / 'rootfs.dmg').exists())

    def test_existing_mismatch_is_preserved(self):
        self.make_archive()
        target = self.dest / 'rootfs.dmg'
        target.write_bytes(b'old untrusted material')
        with self.assertRaisesRegex(ValueError, 'Image mismatch'):
            module.prepare(self.ipsw, self.kernel)
        self.assertEqual(target.read_bytes(), b'old untrusted material')

    def test_symlink_target_is_not_written(self):
        self.make_archive()
        elsewhere = self.root / 'elsewhere'
        elsewhere.write_bytes(b'preserve')
        (self.dest / 'rootfs.dmg').symlink_to(elsewhere)
        with self.assertRaisesRegex(ValueError, 'regular local input'):
            module.prepare(self.ipsw, self.kernel)
        self.assertEqual(elsewhere.read_bytes(), b'preserve')

    def test_interrupted_extraction_is_preserved(self):
        self.make_archive()
        partial = self.dest / 'rootfs.dmg.extracting'
        partial.write_bytes(b'interrupted')
        with self.assertRaises(FileExistsError):
            module.prepare(self.ipsw, self.kernel)
        self.assertEqual(partial.read_bytes(), b'interrupted')
        self.assertFalse((self.dest / 'rootfs.dmg').exists())


class ExtractionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dest = Path(self.tmp.name)
        for name in ['extract-cryptex.log', 'extract-dyld.log', 'extract-cryptex-app.log']:
            (self.dest / name).write_text('')
        for name in ['cryptex', '20F66__iPhone10,3_6', 'cryptex-app']:
            (self.dest / name).mkdir()

    def test_compressed_file_must_be_restored(self):
        (self.dest / 'extract-cryptex.log').write_text('ERROR: Unsupported Method : example\n')
        (self.dest / 'cryptex/example').touch()
        with self.assertRaisesRegex(ValueError, 'not restored'):
            module.verify_extraction(self.dest, 'unused')
        (self.dest / 'cryptex/example').write_bytes(b'decompressed')
        report = module.verify_extraction(self.dest, 'unused')
        self.assertEqual(report['expanded_files_checked']['cryptex'], 1)

    def test_unknown_error_is_not_ignored(self):
        (self.dest / 'extract-dyld.log').write_text('ERROR: Data Error : cache\n')
        with self.assertRaisesRegex(ValueError, 'Unhandled extraction error'):
            module.verify_extraction(self.dest, 'unused')

    def test_compression_attributes_must_be_consumed(self):
        (self.dest / 'cryptex-app/example:com.apple.decmpfs').touch()
        with self.assertRaisesRegex(ValueError, 'Unprocessed compression attributes'):
            module.verify_extraction(self.dest, 'unused')

    def test_restore_only_the_metadata_verified_link(self):
        lib = self.dest / 'cryptex/usr/lib'
        lib.mkdir(parents=True)
        (lib / 'libstdc++.6.0.9.dylib').write_bytes(b'library')
        (lib / 'libstdc++.6.dylib').symlink_to('libstdc++.6.0.9.dylib')
        target = lib / 'libstdc++.dylib'
        target.touch()
        (self.dest / 'extract-cryptex.log').write_text(
            'ERROR: Dangerous link via another link was ignored : usr/lib/libstdc++.dylib : libstdc++.6.dylib\n')
        with patch.object(module.subprocess, 'check_output', return_value='wrong metadata'):
            with self.assertRaisesRegex(ValueError, 'symlink metadata'):
                module.verify_extraction(self.dest, 'fixture')
        self.assertFalse(target.is_symlink())
        listing = 'Path = usr/lib/libstdc++.dylib\nMode = lrwxr-xr-x\nSymbolic Link = libstdc++.6.dylib\n'
        with patch.object(module.subprocess, 'check_output', return_value=listing):
            module.verify_extraction(self.dest, 'fixture')
        self.assertEqual(target.read_bytes(), b'library')
        self.assertTrue(target.is_symlink())


if __name__ == '__main__':
    unittest.main()
