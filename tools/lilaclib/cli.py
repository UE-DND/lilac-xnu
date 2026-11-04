import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from .paths import ROOT, OUT, XNU, HAKIOS, PEPARSE, DEPS, load_lock, local_path, environment, require
from .records import source_state, digest, fingerprint, provenance, run_directory, publish, save_json, workspace_lock

BUILD_TARGETS = ('kernel', 'userspace', 'system', 'xperable', 'mesa')
DEBUG_TARGETS = ('ramboot', 'hardware')


def sources_check():
    errors = []
    lock = load_lock()
    gitlinks = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '--stage'], capture_output=True, text=True)
    if any(line.startswith('160000 ') for line in gitlinks.stdout.splitlines()):
        errors.append('source snapshots must not contain submodule gitlinks')
    if (ROOT / '.gitmodules').exists():
        errors.append('source snapshots must not contain .gitmodules')
    for name, project in lock['projects'].items():
        if not re.fullmatch('[0-9a-f]{40}', project.get('commit', '')):
            errors.append(f'project is not pinned to a full commit: {name}')
        directory = ROOT / project['path']
        if not directory.is_dir():
            errors.append(f'missing source: {name}')
        for relative in project['license_paths']:
            license_path = directory / relative
            if not license_path.is_file():
                errors.append(f'missing license: {name}/{relative}')
        for relative in project.get('excluded', []):
            excluded = directory / relative
            if excluded.exists() or excluded.is_symlink():
                errors.append(f'excluded source content: {name}/{relative}')
    for parent in (ROOT / 'sources', ROOT / 'third_party'):
        for directory, subdirs, files in os.walk(parent):
            if '.git' in subdirs or '.git' in files or '.gitmodules' in files:
                errors.append(f'nested Git metadata: {directory}')
    for name, entry in source_state().items():
        if 'link' in entry:
            path = ROOT / name
            if Path(entry['link']).is_absolute() or not path.exists() or not path.resolve().is_relative_to(ROOT):
                errors.append(f'invalid source link: {name}')
    for original, canonical in lock['source_links'].items():
        target = ROOT / canonical
        if not target.is_file():
            errors.append(f'missing canonical source: {canonical}')
        former = ROOT / original
        if not former.is_symlink() or former.resolve() != target.resolve():
            errors.append(f'missing or incorrect canonical link: {original}')
    if errors:
        raise ValueError('\n'.join(errors))
    print('PASS: source origins, licenses, shared links and canonical implementations')


def doctor(target='host'):
    tools = {name: shutil.which(name) for name in ('git', 'python3', 'clang', 'clang++', 'make', 'cmake', 'pkg-config', '7zz', '7z')}
    inputs = {name: {'path': str(local_path(name)), 'exists': local_path(name).exists()}
              for name in ('ios', 'ios_sdk', 'macos_sdk', 'dtb', 'system_map', 'llvm')}
    prerequisites = {
        'host': [name for name in ('git', 'python3', 'clang', 'clang++') if not tools[name]],
        'kernel': [name for name in ('ios_sdk', 'macos_sdk') if not inputs[name]['exists']],
        'ramboot': [name for name in ('dtb', 'llvm') if not inputs[name]['exists']],
        'hardware': [name for name in ('ios_sdk', 'dtb', 'llvm') if not inputs[name]['exists']],
        'userspace': [name for name in ('ios', 'ios_sdk', 'dtb', 'llvm') if not inputs[name]['exists']],
        'system': [name for name in ('ios', 'ios_sdk') if not inputs[name]['exists']],
        'xperable': [name for name in ('cmake', 'pkg-config') if not tools[name]],
        'mesa': [name for name in ('ios_sdk', 'llvm') if not inputs[name]['exists']],
    }
    for name in ('kernel', 'userspace', 'system', 'mesa', 'hardware'):
        if not (DEPS / 'manifest.json').is_file():
            prerequisites[name].append('prepared dependencies: ./lilac deps fetch, then ./lilac build kernel (or ./lilac deps build for tools only)')
    for name in ('ramboot', 'userspace', 'hardware'):
        if not (OUT / 'latest/kernel/hakios-kernel').is_file():
            prerequisites[name].append('kernel: ./lilac build kernel (or --kernel PATH)')
        for tool in ('clang', 'ld.lld', 'llvm-objcopy', 'llvm-nm'):
            if not (local_path('llvm') / tool).is_file():
                prerequisites[name].append('LLVM ' + tool)
    if tools['pkg-config']:
        for package, targets in (('libusb-1.0', ('xperable',)), ('openssl', ('kernel', 'userspace', 'system', 'hardware'))):
            if subprocess.run(['pkg-config', '--exists', package]).returncode:
                for name in targets:
                    prerequisites[name].append(package)
    print(json.dumps({'platform': sys.platform, 'tools': tools, 'inputs': inputs, 'missing_by_target': prerequisites,
                      'requested_target': target}, indent=2))
    sources_check()
    return 1 if prerequisites['host'] or prerequisites[target] else 0


def xperable(output, jobs, loader=None):
    source = ROOT / 'sources/xperable'
    build = OUT / 'build/xperable'
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=False)
    with (output / 'build.log').open('w') as log:
        subprocess.run(['cmake', '-S', str(PEPARSE), '-B', str(build / 'pe-parse'),
                        '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_COMMAND_LINE_TOOLS=OFF',
                        '-DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations',
                        '-DPEPARSE_ENABLE_TESTING=OFF', '-DPEPARSE_ENABLE_EXAMPLES=OFF'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['cmake', '--build', str(build / 'pe-parse'), '--parallel', str(jobs)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        import shlex
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'libusb-1.0'], text=True))
        libraries = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'libusb-1.0'], text=True))
        objects = []
        for name in ('xperable.c', 'fbusb.c', 'pe-load.cpp'):
            target = build / (Path(name).stem + '.o')
            compiler = 'clang++' if name.endswith('.cpp') else 'clang'
            subprocess.run([compiler, '-g', '-D_GNU_SOURCE', '-DTARGET_ABL_P114',
                            *(['-std=c++17'] if name.endswith('.cpp') else []), *flags,
                            '-I' + str(PEPARSE / 'pe-parser-library/include'),
                            '-c', str(source / name), '-o', str(target)], stdout=log, stderr=subprocess.STDOUT, check=True)
            objects.append(str(target))
        subprocess.run(['clang++', *objects, str(build / 'pe-parse/pe-parser-library/libpe-parse.a'),
                        *libraries, '-o', str(output / 'xperable')], stdout=log, stderr=subprocess.STDOUT, check=True)
    if loader:
        shutil.copy2(require(loader, 'extracted p114 LinuxLoader PE'), output / 'LinuxLoader-p114.pe')
    save_json(output / 'manifest.json', {'target': 'p114', 'readonly_survey': False,
              'binary_sha256': digest(output / 'xperable'),
              'compiler': subprocess.check_output(['clang', '--version'], text=True).strip(),
              'libusb_version': subprocess.check_output(['pkg-config', '--modversion', 'libusb-1.0'], text=True).strip(),
              'loader_sha256': digest(output / 'LinuxLoader-p114.pe') if loader else None,
              'firmware': 'Provide LinuxLoader-p114.pe in the runtime working directory; build does not download firmware.'})


def build(target, arguments, output=None, jobs=8):
    output = (Path(output).expanduser().absolute() if output else run_directory(target))
    if output.exists():
        raise ValueError(f'output already exists: {output}')
    if any(output.resolve().is_relative_to((ROOT / name).resolve()) for name in
           ('sources', 'third_party', 'shared', 'bootloader', 'kernel', 'userland', 'tools', 'configs', 'local')):
        raise ValueError('output must not be inside source or input directories')
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        execute_build(target, arguments, output, jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        output.mkdir(parents=True, exist_ok=True)
        if target == 'kernel' and (DEPS / 'setup.log').is_file():
            shutil.copy2(DEPS / 'setup.log', output / 'dependencies.log')
        save_json(output / 'failure.json', {'status': 'failed', 'error': str(error),
                  'command': ['./lilac', 'debug' if target in DEBUG_TARGETS else 'build', target,
                              '--output', str(output), '-j', str(jobs), *arguments]})
        print(f'Failure report: {output}', file=sys.stderr)
        raise
    manifest_path = output / 'manifest.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    manifest['build_provenance'] = provenance()
    save_json(manifest_path, manifest)
    if target == 'kernel' and (DEPS / 'setup.log').is_file():
        shutil.copy2(DEPS / 'setup.log', output / 'dependencies.log')
    return output


def execute_build(target, arguments, output, jobs):
    from . import kernel
    if target == 'kernel':
        if arguments:
            raise ValueError('unrecognized kernel arguments: ' + ' '.join(arguments))
        from . import deps
        deps.build(jobs)
        kernel.build(output, jobs)
    elif target == 'xperable':
        options = argparse.ArgumentParser(prog='./lilac build xperable')
        options.add_argument('--loader', type=Path, help='already extracted p114 LinuxLoader PE; copied into output')
        settings = options.parse_args(arguments)
        xperable(output, jobs, settings.loader)
    elif target == 'mesa':
        if arguments:
            raise ValueError('unrecognized mesa arguments: ' + ' '.join(arguments))
        from .mesa import build as build_mesa
        build_mesa(output, jobs)
    else:
        forwarded = list(arguments)
        if target in ('ramboot', 'userspace', 'hardware') and not any(value == '--kernel' or value.startswith('--kernel=') for value in forwarded):
            kernel_path = require(OUT / 'latest/kernel/hakios-kernel', 'kernel; run ./lilac build kernel')
            forwarded += ['--kernel', str(kernel_path.resolve())]
        subprocess.run([sys.executable, '-m', 'tools.lilaclib.' + target, '--output', str(output), *forwarded],
                       cwd=ROOT, env=environment(), check=True)


def test(group, build_path=None):
    from .testing import run
    return run(group, build_path)


def parser():
    root = argparse.ArgumentParser(prog='./lilac', description='Lilac source and build workspace')
    commands = root.add_subparsers(dest='command', required=True)
    diagnostic = commands.add_parser('doctor')
    diagnostic.add_argument('--target', choices=('host', *BUILD_TARGETS, *DEBUG_TARGETS), default='host')
    sources = commands.add_parser('sources')
    sources.add_argument('action', choices=['check'])
    deps = commands.add_parser('deps')
    deps.add_argument('action', choices=['fetch', 'build'])
    deps.add_argument('--offline', action='store_true')
    deps.add_argument('--seed', type=Path)
    deps.add_argument('--with-mesa', action='store_true')
    deps.add_argument('-j', '--jobs', type=int, default=8)
    inputs = commands.add_parser('inputs')
    inputs.add_argument('kind', choices=['ios'])
    inputs.add_argument('--ipsw', type=Path, required=True)
    inputs.add_argument('--unpack', action='store_true')
    for name, targets, description in (
            ('build', BUILD_TARGETS, 'Build kernel, runtime, system image or host/graphics tools'),
            ('debug', DEBUG_TARGETS, 'Build advanced RAM boot and hardware diagnostic images')):
        builder = commands.add_parser(name, help=description)
        builder.add_argument('target', choices=targets)
        builder.add_argument('--output', type=Path)
        builder.add_argument('-j', '--jobs', type=int, default=8)
    tests = commands.add_parser('test')
    tests.add_argument('group', choices=['host', 'integration'])
    tests.add_argument('--build', type=Path)
    commands.add_parser('clean')
    device = commands.add_parser('device')
    device.add_argument('action', choices=['run', 'collect'])
    device.add_argument('--build', type=Path, required=True)
    device.add_argument('--fastboot', type=Path, required=True)
    device.add_argument('--serial', required=True)
    device.add_argument('--timeout', type=int, default=600)
    device.add_argument('--reboot-bootloader', action='store_true')
    return root


def run_command(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    if len(arguments) >= 3 and '--help' in arguments and (
            arguments[0] == 'build' and arguments[1] in ('userspace', 'system') or
            arguments[0] == 'debug' and arguments[1] in DEBUG_TARGETS):
        return subprocess.call([sys.executable, '-m', 'tools.lilaclib.' + arguments[1], '--help'], cwd=ROOT, env=environment())
    arg_parser = parser()
    args, extra = arg_parser.parse_known_args(arguments)
    if extra and args.command not in ('build', 'debug'):
        arg_parser.error('unrecognized arguments: ' + ' '.join(extra))
    if getattr(args, 'jobs', 1) < 1:
        arg_parser.error('--jobs must be positive')
    before = source_state() if args.command in ('build', 'debug', 'deps', 'inputs', 'test') else None
    result = 0
    completed_output = None
    try:
        if args.command == 'doctor':
            result = doctor(args.target)
        elif args.command == 'sources':
            sources_check()
        elif args.command == 'deps':
            from . import deps
            if args.action == 'fetch':
                deps.fetch(args.offline, args.seed, args.with_mesa)
            else:
                deps.build(args.jobs)
        elif args.command == 'inputs':
            subprocess.run([sys.executable, '-m', 'tools.lilaclib.ios', '--ipsw', str(args.ipsw),
                            *(['--unpack'] if args.unpack else [])], cwd=ROOT, env=environment(), check=True)
        elif args.command in ('build', 'debug'):
            completed_output = build(args.target, extra, args.output, args.jobs)
        elif args.command == 'test':
            result = test(args.group, args.build)
        elif args.command == 'device':
            from . import device
            if not 1 <= args.timeout <= 3600:
                raise ValueError('--timeout must be between 1 and 3600 seconds')
            result = device.run(args)
        elif args.command == 'clean':
            directory = OUT / 'build'
            if directory.is_symlink():
                raise ValueError('refusing to clean a symlink')
            if directory.exists():
                shutil.rmtree(directory)
            print('Removed build cache; dependencies, inputs and successful artifacts retained')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'lilac: {error}', file=sys.stderr)
        result = 1
    finally:
        if before is not None:
            after = source_state()
            changed = [name for name in before.keys() | after.keys() if before.get(name) != after.get(name)]
            if changed:
                print('lilac: source tree changed during command: ' + ', '.join(sorted(changed)), file=sys.stderr)
                result = 1
    if result == 0 and completed_output is not None:
        publish(args.target, completed_output)
        print(f'Built {args.target}: {completed_output}')
    return result


def main(argv=None):
    try:
        with workspace_lock():
            return run_command(argv)
    except (OSError, ValueError) as error:
        print(f'lilac: {error}', file=sys.stderr)
        return 1
