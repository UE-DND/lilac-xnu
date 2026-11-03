import datetime
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

from .paths import ROOT, OUT


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def save_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
        temporary = Path(stream.name)
    os.replace(temporary, path)


def inventory(directory, include_directories=False, include_generated=False):
    directory = Path(directory)
    records = {}
    for parent, directories, filenames in os.walk(directory, followlinks=False):
        if include_directories:
            records[Path(parent).relative_to(directory).as_posix()] = {'directory': True, 'mode': stat.S_IMODE(Path(parent).stat().st_mode)}
        excluded = {'.git'} if include_generated else {'.git', '__pycache__'}
        directories[:] = sorted(name for name in directories if name not in excluded)
        for name in sorted(filenames + [name for name in directories if (Path(parent) / name).is_symlink()]):
            path = Path(parent) / name
            key = path.relative_to(directory).as_posix()
            if path.is_symlink():
                records[key] = {'link': os.readlink(path)}
            elif path.is_file():
                records[key] = {'sha256': digest(path), 'mode': stat.S_IMODE(path.stat().st_mode)}
            else:
                raise ValueError(f'unsupported input file type: {path}')
    return records


def source_state():
    state = {}
    for name in ('sources', 'third_party', 'bootloader', 'kernel', 'shared', 'userland', 'tools', 'configs'):
        directory = ROOT / name
        if directory.exists():
            state.update({f'{name}/{path}': value for path, value in inventory(directory, include_directories=True, include_generated=True).items() if path != 'lilac/system-map.bin'})
    for name in ('lilac', 'upstream.lock.json'):
        if (ROOT / name).is_file():
            state[name] = {'sha256': digest(ROOT / name), 'mode': stat.S_IMODE((ROOT / name).stat().st_mode)}
    return state


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def run_directory(target):
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    return OUT / 'runs' / f'{stamp}-{target}'


def publish(target, output):
    latest = OUT / 'latest'
    latest.mkdir(parents=True, exist_ok=True)
    temporary = latest / f'.{target}-{os.getpid()}'
    temporary.symlink_to(os.path.relpath(output, latest))
    os.replace(temporary, latest / target)


def provenance(state=None):
    git = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True, capture_output=True)
    status = subprocess.run(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=normal'], text=True, capture_output=True)
    return {'revision': git.stdout.strip(), 'dirty': bool(status.stdout),
            'source_fingerprint': fingerprint(state if state is not None else source_state()),
            'lock_sha256': digest(ROOT / 'upstream.lock.json'),
            'dependencies_manifest': json.loads((OUT / 'deps/manifest.json').read_text()) if (OUT / 'deps/manifest.json').is_file() else None}


@contextmanager
def workspace_lock():
    if OUT.is_symlink():
        raise ValueError('out/ must not be a symlink')
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / '.workspace.lock').open('a') as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('another lilac command is using this workspace') from None
        try:
            yield
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)
