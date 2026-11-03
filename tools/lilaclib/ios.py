#!/usr/bin/env python3
"""Verify iOS 16.5 inputs and optionally unpack them in this workspace."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import zipfile

from .paths import ROOT, HAKIOS, local_path, environment
IPSW_SIZE = 6165698198
IPSW_SHA256 = 'c1091c401f67e3aa7cdfeb2cc19e6c79be3c957aae4e097a019e9ccea07d52f2'
PARTS = (
    ('096-04088-077.dmg', 'rootfs.dmg', 4728546953),
    ('096-04666-078.dmg', 'cryptex-os.dmg', 2726297600),
    ('096-03536-083.dmg', 'cryptex-app.dmg', 10485760),
)
CHUNK = 8 * 1024 * 1024


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(CHUNK), b''):
            sha.update(block)
    return sha.hexdigest()


def prepare(ipsw, dest):
    if not dest.is_dir() or dest.is_symlink():
        raise ValueError('Expected an independent local iOS input directory')
    print('Checking complete IPSW size and SHA-256...', flush=True)
    if ipsw.stat().st_size != IPSW_SIZE or digest(ipsw) != IPSW_SHA256:
        raise ValueError('IPSW size or SHA-256 mismatch; no images extracted')
    records = []
    with zipfile.ZipFile(ipsw) as archive:
        names = archive.namelist()
        for name in ['BuildManifest.plist'] + [p[0] for p in PARTS]:
            if names.count(name) != 1:
                raise ValueError('Missing or duplicated IPSW member: ' + name)
        manifest = plistlib.loads(archive.read('BuildManifest.plist'))
        if (manifest.get('ProductVersion') != '16.5' or
                manifest.get('ProductBuildVersion') != '20F66' or
                set(manifest.get('SupportedProductTypes', [])) != {'iPhone10,3', 'iPhone10,6'}):
            raise ValueError('Unexpected BuildManifest identity')
        for member, name, size in PARTS:
            if archive.getinfo(member).file_size != size:
                raise ValueError('Unexpected member size: ' + member)
            target = dest / name
            if target.is_symlink() or (target.exists() and not target.is_file()):
                raise ValueError('Expected a regular local input: ' + str(target))
        for member, name, size in PARTS:
            target = dest / name
            partial = target.with_name(target.name + '.extracting')
            existing = target.exists()
            output = None if existing else partial.open('xb')
            sha = hashlib.sha256()
            try:
                with archive.open(member) as stream:
                    for block in iter(lambda: stream.read(CHUNK), b''):
                        sha.update(block)
                        if output is not None:
                            output.write(block)
                if output is not None:
                    output.flush()
                    os.fsync(output.fileno())
            finally:
                if output is not None:
                    output.close()
            checked = target if existing else partial
            if checked.stat().st_size != size or digest(checked) != sha.hexdigest():
                raise ValueError('Image mismatch; retained without overwrite: ' + str(checked))
            if not existing:
                # Publish without replacing a file created during extraction.
                os.link(partial, target)
                partial.unlink()
            records.append({'member': member, 'file': name, 'size': size, 'sha256': sha.hexdigest()})
            print('Verified ' + name, flush=True)
    provenance = {
        'ipsw': str(ipsw.resolve()), 'ipsw_size': IPSW_SIZE, 'ipsw_sha256': IPSW_SHA256,
        'product_version': '16.5', 'build': '20F66', 'images': records,
        'unpack_script_sha256': digest(HAKIOS / 'sysimage/unpack.sh'),
        'status': 'images verified; unpack not run by this invocation',
    }
    save_provenance(dest, provenance)
    return provenance


def save_provenance(dest, provenance):
    path = dest / 'input-provenance.json'
    if path.is_symlink():
        raise ValueError('Refusing a provenance symlink')
    with tempfile.NamedTemporaryFile(mode='w', dir=dest, prefix='.provenance-', delete=False) as out:
        json.dump(provenance, out, indent=2)
        out.write('\n')
        temporary = Path(out.name)
    os.replace(temporary, path)


def find_sevenzip():
    exe = shutil.which('7zz') or shutil.which('7z')
    if exe is None:
        raise ValueError('APFS-capable 7-Zip 23 or newer is required (7zz or 7z)')
    result = subprocess.run([exe, 'i'], check=True, text=True, capture_output=True)
    if 'APFS' not in result.stdout:
        raise ValueError('Installed 7-Zip does not advertise APFS support')
    return exe


def verify_extraction(dest, sevenzip):
    """Account for 7-Zip errors that upstream intentionally leaves to unzbm."""
    link_name = 'usr/lib/libstdc++.dylib'
    link_target = 'libstdc++.6.dylib'
    ignored_link = 'ERROR: Dangerous link via another link was ignored : ' + link_name + ' : ' + link_target
    counts = {}
    repaired = []
    for log_name, directory in [('extract-cryptex.log', 'cryptex'),
                                ('extract-dyld.log', '20F66__iPhone10,3_6'),
                                ('extract-cryptex-app.log', 'cryptex-app')]:
        count = 0
        for line in (dest / log_name).read_text(errors='replace').splitlines():
            prefix = 'ERROR: Unsupported Method : '
            if line.startswith(prefix):
                relative = line[len(prefix):]
                if log_name == 'extract-dyld.log':
                    cache_prefix = 'System/Library/Caches/com.apple.dyld/'
                    if not relative.startswith(cache_prefix):
                        raise ValueError('Unexpected cache extraction path: ' + relative)
                    relative = relative[len(cache_prefix):]
                path = dest / directory / relative
                if (Path(relative).is_absolute() or '..' in Path(relative).parts or
                        not path.is_file() or path.stat().st_size == 0):
                    raise ValueError('Compressed file was not restored: ' + str(path))
                count += 1
            elif line == ignored_link and directory == 'cryptex':
                listing = subprocess.check_output(
                    [sevenzip, 'l', '-slt', str(dest / 'cryptex-os.dmg'), link_name], text=True)
                required = ['Path = ' + link_name, 'Mode = lrwxr-xr-x', 'Symbolic Link = ' + link_target]
                if not all(value in listing.splitlines() for value in required):
                    raise ValueError('Unexpected cryptex symlink metadata')
                base = dest / directory
                path = base / link_name
                resolved = (path.parent / link_target).resolve(strict=True)
                if not resolved.is_relative_to(base.resolve()) or not resolved.is_file():
                    raise ValueError('Cryptex symlink target is outside its local tree')
                if path.is_symlink():
                    if os.readlink(path) != link_target:
                        raise ValueError('Existing cryptex symlink differs from the image')
                else:
                    if path.exists() and (not path.is_file() or path.stat().st_size != 0):
                        raise ValueError('Refusing to replace a nonempty cryptex file')
                    temporary = path.with_name(path.name + '.restoring')
                    temporary.symlink_to(link_target)
                    os.replace(temporary, path)
                repaired.append(link_name + ' -> ' + link_target)
            elif line.startswith('ERROR:'):
                raise ValueError('Unhandled extraction error: ' + line)
        counts[directory] = count
        if any((dest / directory).rglob('*:com.apple.*')):
            raise ValueError('Unprocessed compression attributes remain in ' + directory)
    return {'expanded_files_checked': counts, 'verified_symlinks': repaired}


def unpack(ipsw, dest, provenance, sevenzip):
    for name in ['rootfs', 'cryptex', 'cryptex.cache', 'cryptex-app', '20F66__iPhone10,3_6']:
        if (dest / name).is_symlink():
            raise ValueError('Refusing a shared extraction directory: ' + name)
    # Upstream calls 7z; Homebrew supplies 7zz. Keep this alias local to the run.
    with tempfile.TemporaryDirectory(prefix='lilac-7z-') as bindir:
        Path(bindir, '7z').symlink_to(sevenzip)
        env = {**environment(), 'LILAC_SYSIMAGE': str(dest), 'KEEP': '1', 'PATH': bindir + os.pathsep + os.environ.get('PATH', '')}
        env.pop('ANY_IPSW', None)
        subprocess.run(['bash', str(HAKIOS / 'sysimage/unpack.sh'), str(ipsw.resolve())], cwd=ROOT, env=env, check=True)
    provenance['extraction_checks'] = verify_extraction(dest, sevenzip)
    required = ['rootfs/sbin/launchd', 'system.manifest', 'var.skel',
                'cryptex-app/usr/libexec/webbookmarksd', 'DeviceTree.d22ap.im4p']
    for name in required:
        if not (dest / name).is_file() or (dest / name).stat().st_size == 0:
            raise ValueError('Unpack output missing or empty: ' + name)
    cache = dest / '20F66__iPhone10,3_6'
    if not cache.is_dir() or len(list(cache.iterdir())) != 43:
        raise ValueError('Expected 43 dyld shared cache files')
    provenance['status'] = 'unpacked; core outputs verified on host'
    provenance['sevenzip'] = sevenzip
    provenance['outputs'] = [{'file': name, 'size': (dest / name).stat().st_size,
                               'sha256': digest(dest / name)} for name in required]
    save_provenance(dest, provenance)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ipsw', type=Path, required=True)
    parser.add_argument('--unpack', action='store_true', help='also run local sysimage/unpack.sh')
    args = parser.parse_args()
    try:
        sevenzip = find_sevenzip() if args.unpack else None
        destination = local_path('ios')
        destination.mkdir(parents=True, exist_ok=True)
        provenance = prepare(args.ipsw, destination)
        if args.unpack:
            unpack(args.ipsw, destination, provenance, sevenzip)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, 'prepare-ios: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
