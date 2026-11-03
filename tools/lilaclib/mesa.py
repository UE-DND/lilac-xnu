import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

from .paths import ROOT, HAKIOS, OUT, DEPS, local_path, load_lock, environment, require
from .records import digest, fingerprint, inventory, save_json


def build(output, jobs):
    from .deps import fetch
    fetch(offline=True, mesa=True)
    dependency = load_lock()['dependencies']['mesa']
    for name, expected in dependency['patches'].items():
        if digest(ROOT / name) != expected:
            raise ValueError(f'Mesa patch changed; update its lock entry: {name}')
    require(DEPS / 'host/bin/xnu-clang', 'prepared toolchain; run ./lilac deps build')
    llvm = local_path('llvm')
    for tool in ('llvm-ar', 'llvm-ranlib', 'llvm-strip'):
        require(llvm / tool, 'LLVM tool')
    bison = shutil.which('bison')
    if not bison:
        raise ValueError('Mesa requires Bison 3 or newer on PATH')
    bison_version = subprocess.check_output([bison, '--version'], text=True).splitlines()[0]
    if int(re.search(r'\d+', bison_version).group()) < 3:
        raise ValueError('Mesa requires Bison 3 or newer; macOS: prepend $(brew --prefix bison)/bin to PATH')
    key = fingerprint({'dependency': dependency, 'recipes': inventory(HAKIOS / 'tools/mesa'),
                       'script': digest(HAKIOS / 'tools/mk-mesa.sh'), 'orchestrator': digest(__file__),
                       'dependencies': digest(DEPS / 'manifest.json'), 'llvm': str(llvm),
                       'bison': {'path': bison, 'version': bison_version},
                       'drivers': os.environ.get('DRIVERS', 'softpipe,freedreno'), 'air': os.environ.get('AIR', '')})[:20]
    directory = OUT / 'build/mesa' / key
    directory.mkdir(parents=True, exist_ok=True)
    archive = OUT / 'cache' / Path(dependency['url']).name
    target = directory / archive.name
    if not target.exists():
        shutil.copy2(archive, target)
    venv = directory / 'venv'
    if not (venv / 'bin/python').exists():
        subprocess.run([sys.executable, '-m', 'venv', str(venv)], check=True)
    output.mkdir(parents=True, exist_ok=False)
    with (output / 'build.log').open('w') as log:
        subprocess.run([str(venv / 'bin/python'), '-m', 'pip', 'install', '--no-index',
                        '--find-links', str(OUT / 'cache/mesa-python'), '--require-hashes',
                        '-r', str(OUT / 'cache/mesa-python/requirements.txt')],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        env = {**environment(), 'LILAC_MESA_BUILD': str(directory), 'LILAC_MESA_VERSION': dependency['version'],
               'LILAC_MESA_SHA256': dependency['sha256'], 'JOBS': str(jobs),
               'PATH': os.pathsep.join((str(venv / 'bin'), str(llvm), str(DEPS / 'host/bin'), os.environ['PATH']))}
        try:
            subprocess.run(['bash', str(HAKIOS / 'tools/mk-mesa.sh')], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        finally:
            for name in ('meson-setup.log', 'ninja.log'):
                if (directory / name).is_file():
                    shutil.copy2(directory / name, output / name)
    for source in (directory / 'out').iterdir():
        if source.is_file():
            shutil.copy2(source, output / source.name)
    save_json(output / 'manifest.json', {'mesa': dependency, 'byte_reproducible': False,
              'python_lock_sha256': digest(ROOT / 'configs/mesa-python.lock.json'),
              'outputs': {path.name: digest(path) for path in output.iterdir() if path.is_file()}})
