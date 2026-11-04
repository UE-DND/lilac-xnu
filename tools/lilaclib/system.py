#!/usr/bin/env python3
"""Build a disk-resident APFS volume with the original, unsplit iOS cache."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from .paths import HAKIOS, DEPS, local_path
from .files import copy_file
from .probes import build_dynamic_probe, dynamic_probe_inputs
from .records import digest


def build(output, probe_binary=None):
    if probe_binary is not None:
        probe_binary = Path(probe_binary).expanduser().resolve()
        if not probe_binary.is_file():
            raise ValueError(f'missing probe binary: {probe_binary}')
    output.mkdir(parents=True, exist_ok=False)
    out = output.resolve()
    sysimage = local_path('ios')
    probe_inputs = []
    if probe_binary is None:
        probe_binary = out / 'obj/hello_dyn'
        with (out / 'dynamic-build.log').open('w') as log:
            build_dynamic_probe(probe_binary, log)
        probe_inputs = dynamic_probe_inputs()
    source = sysimage / 'rootfs'
    cache = sysimage / '20F66__iPhone10,3_6'
    shutil.copytree(source, out / 'root', copy_function=copy_file, symlinks=True)
    cacheout = out / 'root/System/Library/Caches/com.apple.dyld'
    cacheout.mkdir(parents=True)
    caches = sorted(cache.glob('dyld_shared_cache_arm64*'))
    assert len(caches) == 43
    for path in caches:
        copy_file(path, cacheout / path.name)
    copy_file(probe_binary, out / 'root/bin/hello_dyn')
    probe = out / 'root/lilac-probe'
    probe.mkdir()
    pattern = bytes((i * 73 + (i >> 8) * 19 + 41) & 255 for i in range(65573))
    (probe / 'pattern.bin').write_bytes(pattern)
    with (out / 'build.log').open('w') as log:
        subprocess.run([sys.executable, str(HAKIOS / 'tools/apfs/mkapfs.py'), '-o', str(out / 'system.apfs'),
                        'LilacSystem=' + str(out / 'root') + ',manifest=' + str(sysimage / 'system.manifest')],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    reader = DEPS / 'host/bin/apfs-tool'
    checked = {}
    for name in ['/usr/lib/dyld','/sbin/launchd','/bin/hello_dyn','/lilac-probe/pattern.bin']:
        data = subprocess.check_output([str(reader), 'cat', str(out / 'system.apfs'), name])
        assert data == (out / 'root' / name[1:]).read_bytes(), name
        checked[name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    # Read all 43 cache files back, hashing a stream instead of loading 2.72GB.
    cache_info = []
    for path in caches:
        name = '/System/Library/Caches/com.apple.dyld/' + path.name
        proc = subprocess.Popen([str(reader), 'cat', str(out / 'system.apfs'), name], stdout=subprocess.PIPE)
        h = hashlib.sha256()
        n = 0
        with proc.stdout:
            while True:
                chunk = proc.stdout.read(4 << 20)
                if not chunk:
                    break
                h.update(chunk)
                n += len(chunk)
        assert proc.wait() == 0 and n == path.stat().st_size and h.hexdigest() == digest(path), path.name
        cache_info.append({'name': path.name, 'bytes': n, 'sha256': h.hexdigest()})
    inputs = [Path(__file__).resolve(), HAKIOS / 'tools/apfs/mkapfs.py', reader, probe_binary, *probe_inputs]
    manifest = {
        'purpose': 'original selected iOS root tree and complete unchanged 43-file shared cache; APFS disk image for UFS file-backed storage',
        'original_shared_cache_not_split': True, 'cache_files': cache_info,
        'cache_bytes': sum(item['bytes'] for item in cache_info),
        'source_tree': str(source), 'source_manifest_sha256': digest(sysimage / 'system.manifest'),
        'apfs_readback': checked, 'image_bytes': (out / 'system.apfs').stat().st_size,
        'image_sha256': digest(out / 'system.apfs'), 'device_writes': 'not performed by this builder',
        'inputs': {str(path): digest(path) for path in inputs},
    }
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({k: v for k, v in manifest.items() if k not in ('cache_files', 'inputs')}, indent=2))


def main():
    parser = argparse.ArgumentParser(prog='./lilac build system', description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--probe-binary', type=Path,
                        help='use an existing hello_dyn; default compiles it independently')
    args = parser.parse_args()
    build(args.output, args.probe_binary)


if __name__ == '__main__':
    main()
