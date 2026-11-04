import plistlib
from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lilaclib.ramstate import migration_header


class MigrationStateTests(unittest.TestCase):
    def prepare(self, directory, state, build='20F66'):
        directory = Path(directory)
        template, version, output = (directory/name for name in ('state.plist', 'version.plist', 'state.h'))
        template.write_bytes(plistlib.dumps(state))
        version.write_bytes(plistlib.dumps({'ProductBuildVersion': build}))
        return template, version, output

    def test_embedded_preference_is_exact_and_matches_build(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = self.prepare(directory, {'LastSystemVersion': '20F66',
                'DMLastMigrationResults': {'success': True, 'buildVersion': '20F66'}})
            report = migration_header(*paths)
            encoded = paths[2].read_text().split('{', 1)[1].split('}', 1)[0]
            self.assertEqual(bytes(map(int, re.findall(r'\d+', encoded))), paths[0].read_bytes())
            self.assertFalse(report['migration_executed'])
            self.assertEqual(report['build_version'], '20F66')

    def test_stale_or_incomplete_state_cannot_skip_migration(self):
        for state in ({'LastSystemVersion': '20F65'},
                      {'LastSystemVersion': '20F66', 'DMLastMigrationResults': {'success': False, 'buildVersion': '20F66'}},
                      {'LastSystemVersion': '20F66', 'DMLastMigrationResults': {'success': True, 'buildVersion': '20F65'}}):
            with self.subTest(state=state), tempfile.TemporaryDirectory() as directory:
                paths = self.prepare(directory, state)
                with self.assertRaisesRegex(ValueError, 'does not match'):
                    migration_header(*paths)
                self.assertFalse(paths[2].exists())


if __name__ == '__main__':
    unittest.main()
