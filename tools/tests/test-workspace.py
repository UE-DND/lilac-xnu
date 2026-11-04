#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lilaclib import cli, deps, files, paths, records


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.source = self.root / 'source'
        self.source.mkdir()
        self.target = self.root / 'target'
        (self.source / 'data').write_bytes(b'original bytes')
        (self.source / 'data').chmod(0o640)

    def test_material_copy_preserves_mode_without_shared_writes(self):
        original = self.source / 'data'
        files.copy_file(original, self.target)
        self.assertEqual(original.read_bytes(), self.target.read_bytes())
        self.assertEqual(original.stat().st_mode, self.target.stat().st_mode)
        self.assertNotEqual(original.stat().st_ino, self.target.stat().st_ino)
        self.target.write_bytes(b'changed copy')
        self.assertEqual(original.read_bytes(), b'original bytes')

    def check_sources(self, source_links, state=None):
        with patch.object(cli, 'ROOT', self.root), \
                patch.object(cli, 'load_lock', return_value={'projects': {}, 'source_links': source_links}), \
                patch.object(cli, 'source_state', return_value=state or {}):
            cli.sources_check()

    def test_required_source_link_cannot_be_missing(self):
        with self.assertRaisesRegex(ValueError, 'canonical link'):
            self.check_sources({'target': 'source/data'})

    def test_required_source_link_cannot_be_replaced_by_copy(self):
        files.copy_file(self.source / 'data', self.target)
        with self.assertRaisesRegex(ValueError, 'canonical link'):
            self.check_sources({'target': 'source/data'})

    def test_relative_canonical_source_link_is_accepted(self):
        self.target.symlink_to('source/data')
        self.check_sources({'target': 'source/data'}, {'target': {'link': 'source/data'}})

    def test_absolute_source_link_is_rejected(self):
        self.target.symlink_to(self.source / 'data')
        with self.assertRaisesRegex(ValueError, 'invalid source link'):
            self.check_sources({'target': 'source/data'}, {'target': {'link': str(self.source / 'data')}})

    def test_configuration_precedence(self):
        configuration = self.root / 'config.json'
        configuration.write_text(json.dumps({'ios': str(self.source)}))
        with patch.object(paths, 'CONFIG_FILE', configuration):
            self.assertEqual(paths.local_path('ios'), self.source)
            self.assertEqual(paths.local_path('ios', self.target), self.target)

    def test_offline_archive_corruption_is_rejected(self):
        cache = self.root / 'cache'
        cache.mkdir()
        (cache / 'mesa.tar.xz').write_bytes(b'corrupt')
        lock = {'dependencies': {'mesa': {'kind': 'archive', 'url': 'https://example.invalid/mesa.tar.xz',
                                         'sha256': hashlib.sha256(b'correct').hexdigest()}}}
        with patch.object(deps, 'OUT', self.root), patch.object(deps, 'load_lock', return_value=lock), \
                patch.object(deps.urllib.request, 'urlopen', side_effect=AssertionError('network access')):
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                deps.fetch(offline=True)

    def test_failed_build_preserves_success_pointer(self):
        artifact = self.root / 'artifact'
        artifact.mkdir()
        with patch.object(records, 'OUT', self.root):
            records.publish('kernel', artifact)
        latest = self.root / 'latest/kernel'
        before = os.readlink(latest)
        with self.assertRaisesRegex(ValueError, 'output already exists'):
            cli.build('kernel', [], artifact)
        self.assertEqual(os.readlink(latest), before)

    def test_command_detects_source_mutation_on_failure(self):
        with patch.object(cli, 'source_state', side_effect=[{'file': 'before'}, {'file': 'after'}]), \
                patch.object(cli, 'build', side_effect=ValueError('build failure')):
            self.assertEqual(cli.run_command(['build', 'kernel']), 1)

    def test_mutated_success_does_not_publish(self):
        with patch.object(cli, 'source_state', side_effect=[{'file': 'before'}, {'file': 'after'}]), \
                patch.object(cli, 'build', return_value=self.target), patch.object(cli, 'publish') as publish:
            self.assertEqual(cli.run_command(['build', 'kernel']), 1)
            publish.assert_not_called()

    def test_build_rejects_source_destination_before_writing(self):
        with patch.object(cli, 'ROOT', self.root), patch.object(cli, 'execute_build') as execute:
            with self.assertRaisesRegex(ValueError, 'source or input'):
                cli.build('kernel', [], self.root / 'sources/new-output')
            execute.assert_not_called()
        self.assertFalse((self.root / 'sources').exists())

    def test_workspace_commands_are_exclusive(self):
        with patch.object(records, 'OUT', self.root):
            with records.workspace_lock():
                with self.assertRaisesRegex(ValueError, 'another lilac command'):
                    with records.workspace_lock():
                        self.fail('second writer acquired the lock')

    def test_failed_build_keeps_report(self):
        with patch.object(cli, 'execute_build', side_effect=ValueError('missing SDK')):
            with self.assertRaises(ValueError):
                cli.build('ramboot', [], self.target)
        self.assertEqual(json.loads((self.target / 'failure.json').read_text())['status'], 'failed')

    def test_source_guard_includes_generated_python_cache(self):
        with patch.object(records, 'ROOT', self.root):
            directory = self.root / 'tools/__pycache__'
            directory.mkdir(parents=True)
            (directory / 'unexpected.pyc').write_bytes(b'generated data')
            self.assertIn('tools/__pycache__/unexpected.pyc', records.source_state())


if __name__ == '__main__':
    unittest.main()
