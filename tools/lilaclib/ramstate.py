"""Prepare an explicit migration bypass for the disposable desktop RAM volume."""
import plistlib
from pathlib import Path


def migration_header(template: Path, system_version: Path, output: Path):
    data = template.read_bytes()
    state = plistlib.loads(data)
    build = plistlib.loads(system_version.read_bytes())['ProductBuildVersion']
    results = state.get('DMLastMigrationResults', {})
    if (state.get('LastSystemVersion') != build or
            results.get('buildVersion') != build or results.get('success') is not True):
        raise ValueError('RAM migration template does not match the system build')
    output.write_text(
        '/* Generated from the upstream RAM preference; this does not run migration. */\n'
        'static const unsigned char ram_migration_plist[] = {\n' +
        ',\n'.join(','.join(str(byte) for byte in data[i:i + 24])
                   for i in range(0, len(data), 24)) + '\n};\n')
    return {'build_version': build,
            'path': '/private/var/mobile/Library/Preferences/com.apple.migration.plist',
            'bytes': len(data), 'migration_executed': False}
