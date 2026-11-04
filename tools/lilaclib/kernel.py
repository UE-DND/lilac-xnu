import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

from .paths import ROOT, XNU, HAKIOS, OUT, DEPS, environment as base_environment, local_path, require
from .records import digest, fingerprint, inventory, save_json

OBJECT_FLAGS = {
    'lilac.touch.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'lilac.buttons.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'lilac.gpu-snapshot.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'lilac.rpm.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'lilac.a540.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'lilac.ufs.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'lilac.ufs-transport.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align -DLILAC_UFS_IO_EXTERNAL=1',
    'lilac.image.o': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'MiniIOMobileFramebuffer.cpo': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'MiniIOSurface.cpo': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
    'hakios.gpu.cpo': '-march=armv8-a -mgeneral-regs-only -mstrict-align',
}


def update_file(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def environment(jobs=8):
    config_path = ROOT / 'configs/lilac/kernel.config'
    compiler = subprocess.check_output(['/usr/bin/clang', '--version'], text=True)
    key = fingerprint({'config': digest(config_path), 'flags': OBJECT_FLAGS, 'compiler': compiler,
                       'drivers': inventory(HAKIOS / 'drivers'),
                       'generator': digest(HAKIOS / 'tools/hakios-config.py'),
                       'ios_sdk': fingerprint(inventory(local_path('ios_sdk'))),
                       'macos_sdk': fingerprint(inventory(local_path('macos_sdk'))),
                       'recipes': {str(path.relative_to(ROOT)): digest(path) for path in
                                   [HAKIOS / 'xnu-build.sh', HAKIOS / 'xnu-env.sh', Path(__file__),
                                    *sorted((XNU / 'makedefs').glob('MakeInc.*'))]}})[:20]
    build = OUT / 'build/kernel' / key
    generated = build / 'generated'
    spec = importlib.util.spec_from_file_location('hakios_config', HAKIOS / 'tools/hakios-config.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if module.DRIVERS_ERROR:
        raise ValueError(module.DRIVERS_ERROR)
    config = module.load(str(config_path))
    problems = module.check_drivers(config, {})
    if problems:
        raise ValueError(str(problems))
    built = module.drivers_built(config)
    update_file(generated / 'MASTER.hakios', module.master_hakios(built, '.config').encode())
    update_file(generated / 'include/hakios.drivers.h', module.drivers_header(built, '.config').encode())
    update_file(generated / '.config', config_path.read_bytes())
    return {**base_environment(), 'LILAC_OBJROOT': str(build / 'obj'),
            'LILAC_GENERATED_DIR': str(generated), 'JOBS': str(jobs),
            'PATH': str(DEPS / 'host/bin') + os.pathsep + os.environ['PATH']}


def build(output, jobs=8):
    env = environment(jobs)
    require(DEPS / 'sdk/iPhoneOS16.5.xnu.sdk/usr/local/lib/kernel/libpthread_kext.a', 'prepared dependencies; run ./lilac deps build')
    output.mkdir(parents=True, exist_ok=False)
    with (output / 'kernel-build.log').open('w') as log:
        subprocess.run(['bash', str(HAKIOS / 'xnu-build.sh'), 'build', f'-j{jobs}',
                        *[name + '_CFLAGS_ADD=' + flags for name, flags in OBJECT_FLAGS.items()]],
                       cwd=HAKIOS, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    kernel = Path(env['LILAC_OBJROOT']) / 'DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
    shutil.copy2(require(kernel), output / 'hakios-kernel')
    save_json(output / 'manifest.json', {'purpose': 'XNU lilac read-only UFS and software display',
              'kernel_bytes': kernel.stat().st_size, 'kernel_sha256': digest(kernel),
              'configuration': 'DEVELOPMENT_ARM64_BCM2837', 'object_flags': OBJECT_FLAGS,
              'generated': inventory(Path(env['LILAC_GENERATED_DIR'])), 'objroot': env['LILAC_OBJROOT'],
              'storage_writes': False})
