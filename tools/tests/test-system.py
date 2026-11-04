#!/usr/bin/env python3
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lilaclib import system


class SystemBuildTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        self.ios = self.root / 'ios'
        for name in ('rootfs/usr/lib/dyld', 'rootfs/sbin/launchd', 'system.manifest'):
            path = self.ios / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        (self.ios / 'rootfs/bin').mkdir()
        cache = self.ios / '20F66__iPhone10,3_6'
        cache.mkdir()
        for index in range(43):
            (cache / f'dyld_shared_cache_arm64.{index}').write_bytes(bytes([index]))
        self.deps = self.root / 'deps'
        reader = self.deps / 'host/bin/apfs-tool'
        reader.parent.mkdir(parents=True)
        reader.write_bytes(b'fixture reader')
        self.output = self.root / 'system'
        self.recipe = self.root / 'probe-recipe'
        self.recipe.write_bytes(b'fixture compiler inputs')

    def compile_probe(self, output, log):
        output.parent.mkdir(parents=True)
        output.write_bytes(b'newly compiled probe')

    def package(self, *args, **kwargs):
        (self.output / 'system.apfs').write_bytes(b'fixture image')

    def readback(self, command, **kwargs):
        return (self.output / 'root' / command[-1].lstrip('/')).read_bytes()

    def run_builder(self, probe_binary=None):
        with patch.object(system, 'local_path', return_value=self.ios), \
                patch.object(system, 'DEPS', self.deps), \
                patch.object(system, 'build_dynamic_probe', side_effect=self.compile_probe) as compile_probe, \
                patch.object(system, 'dynamic_probe_inputs', return_value=[self.recipe]), \
                patch.object(system.subprocess, 'run', side_effect=self.package), \
                patch.object(system.subprocess, 'check_output', side_effect=self.readback), \
                patch.object(system.subprocess, 'Popen', side_effect=lambda command, **kwargs:
                             Mock(stdout=io.BytesIO(self.readback(command)), wait=lambda: 0)), \
                contextlib.redirect_stdout(io.StringIO()):
            system.build(self.output, probe_binary)
        return compile_probe, json.loads((self.output / 'manifest.json').read_text())

    def test_system_builds_its_own_probe_without_userspace_artifacts(self):
        compiler, manifest = self.run_builder()
        compiler.assert_called_once()
        self.assertEqual((self.output / 'root/bin/hello_dyn').read_bytes(), b'newly compiled probe')
        self.assertIn(str(self.recipe), manifest['inputs'])
        self.assertEqual(len(manifest['cache_files']), 43)
        self.assertIn('/bin/hello_dyn', manifest['apfs_readback'])

    def test_explicit_probe_bypasses_compilation_and_is_recorded(self):
        probe = self.root / 'custom-probe'
        probe.write_bytes(b'explicit probe')
        compiler, manifest = self.run_builder(probe)
        compiler.assert_not_called()
        self.assertEqual((self.output / 'root/bin/hello_dyn').read_bytes(), b'explicit probe')
        self.assertIn(str(probe), manifest['inputs'])
        self.assertNotIn(str(self.recipe), manifest['inputs'])

    def test_missing_explicit_probe_fails_before_creating_output(self):
        with self.assertRaisesRegex(ValueError, 'missing probe binary'):
            system.build(self.output, self.root / 'missing')
        self.assertFalse(self.output.exists())

    def test_compilation_failure_does_not_produce_system_image(self):
        with patch.object(system, 'build_dynamic_probe', side_effect=subprocess.CalledProcessError(1, 'compiler')), \
                self.assertRaises(subprocess.CalledProcessError):
            system.build(self.output)
        self.assertFalse((self.output / 'system.apfs').exists())


if __name__ == '__main__':
    unittest.main()
