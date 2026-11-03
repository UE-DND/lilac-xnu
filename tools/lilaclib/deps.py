import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

from .paths import ROOT, OUT, DEPS, HAKIOS, XNU, load_lock, local_path, require
from .records import digest, fingerprint, inventory, save_json


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


def fetch(offline=False, seed=None, mesa=False):
    cache = OUT / 'cache'
    cache.mkdir(parents=True, exist_ok=True)
    for name, dependency in load_lock()['dependencies'].items():
        if dependency.get('optional') and not mesa:
            continue
        if dependency['kind'] == 'archive':
            target = cache / Path(dependency['url']).name
            if not target.exists():
                if offline:
                    raise ValueError(f'archive not cached: {target}')
                temporary = target.with_suffix(target.suffix + '.partial')
                with urllib.request.urlopen(dependency['url']) as response, temporary.open('wb') as stream:
                    shutil.copyfileobj(response, stream)
                if digest(temporary) != dependency['sha256']:
                    raise ValueError(f'archive checksum mismatch: {temporary}')
                temporary.rename(target)
            if digest(target) != dependency['sha256']:
                raise ValueError(f'archive checksum mismatch: {target}')
            continue
        target = cache / 'git' / name
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            candidate = Path(seed) / name if seed else None
            if candidate and (candidate / '.git').exists():
                subprocess.run(['git', 'clone', '--quiet', '--no-hardlinks', str(candidate), str(target)], check=True)
                subprocess.run(['git', '-C', str(target), 'checkout', '--quiet', '--detach', dependency['commit']], check=True)
            elif offline:
                raise ValueError(f'dependency not cached: {name}')
            else:
                subprocess.run(['git', 'init', '--quiet', str(target)], check=True)
                subprocess.run(['git', '-C', str(target), 'fetch', '--quiet', '--depth', '1', dependency['url'], dependency['commit']], check=True)
                subprocess.run(['git', '-C', str(target), 'checkout', '--quiet', '--detach', 'FETCH_HEAD'], check=True)
        if git(target, 'rev-parse', 'HEAD') != dependency['commit'] or git(target, 'status', '--porcelain'):
            raise ValueError(f'cached dependency must be clean at {dependency["commit"]}: {target}')
        print(f'Verified {name}: {dependency["commit"]}')
    if mesa:
        fetch_python(offline)


def fetch_python(offline):
    wheels = OUT / 'cache/mesa-python'
    wheels.mkdir(parents=True, exist_ok=True)
    entry = load_lock()['dependencies']['mesa']['python_lock']
    if digest(ROOT / entry['path']) != entry['sha256']:
        raise ValueError('Mesa Python lock checksum differs from upstream.lock.json')
    locked = json.loads((ROOT / entry['path']).read_text())['packages']
    requirements = wheels / 'requirements.txt'
    requirements.write_text(''.join(name + '==' + package['version'] + ' ' +
                            ' '.join('--hash=sha256:' + value['sha256'] for value in package['files'].values()) + '\n'
                            for name, package in locked.items()))
    if not offline:
        subprocess.run([sys.executable, '-m', 'pip', 'download', '--only-binary=:all:', '--dest', str(wheels),
                        '--require-hashes', '-r', str(requirements)], check=True)
    for name, package in locked.items():
        found = [path for path in wheels.glob('*.whl') if path.name in package['files']]
        if not found:
            raise ValueError(f'Mesa Python wheel is not cached: {name}')
        for path in found:
            if digest(path) != package['files'][path.name]['sha256']:
                raise ValueError(f'Python wheel checksum mismatch: {path}')


def archive(source, target):
    target.mkdir(parents=True, exist_ok=False)
    producer = subprocess.Popen(['git', '-C', str(source), 'archive', 'HEAD'], stdout=subprocess.PIPE)
    try:
        subprocess.run(['tar', '-x', '-C', str(target)], stdin=producer.stdout, check=True)
    finally:
        producer.stdout.close()
    if producer.wait() != 0:
        raise ValueError(f'cannot export {source}')


def copy_support():
    source = HAKIOS / 'xnu-deps'
    for relative in ('host', 'hostinc', 'iig-impl', 'build/lzbitmap', 'build/pthread'):
        for path in (source / relative).rglob('*'):
            if not path.is_file():
                continue
            target = DEPS / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists() or path.read_bytes() != target.read_bytes():
                shutil.copy2(path, target)


def link(source, target):
    if target.is_symlink() and target.resolve() == source.resolve():
        return
    if target.exists() or target.is_symlink():
        raise ValueError(f'conflicting generated input: {target}')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.symlink_to(source.resolve())


def build(jobs=8):
    from . import kernel
    fetch(offline=True)
    require(local_path('ios_sdk') / 'usr/include', 'iPhoneOS 16.5 SDK')
    require(local_path('macos_sdk') / 'System/Library/Frameworks/Kernel.framework/Headers/DriverKit', 'macOS 13.3 DriverKit headers')
    DEPS.mkdir(parents=True, exist_ok=True)
    for name, dependency in load_lock()['dependencies'].items():
        if dependency['kind'] != 'git':
            continue
        source = OUT / 'cache/git' / name
        target = DEPS / name
        marker = DEPS / ('.source-' + name)
        if target.exists():
            if not marker.exists() or marker.read_text().strip() != dependency['commit']:
                raise ValueError(f'unrecognized dependency workspace: {target}')
        else:
            archive(source, target)
            marker.write_text(dependency['commit'] + '\n')
    copy_support()
    link(local_path('macos_sdk'), DEPS / 'MacOSX13.3.sdk')
    env = kernel.environment(jobs)
    sdk_inputs = {name: {'path': str(local_path(name).resolve()), 'fingerprint': fingerprint(inventory(local_path(name)))}
                  for name in ('ios_sdk', 'macos_sdk')}
    sdk_marker = DEPS / 'sdk-inputs.json'
    if not sdk_marker.exists() or json.loads(sdk_marker.read_text()) != sdk_inputs:
        overlay = DEPS / 'sdk/iPhoneOS16.5.xnu.sdk'
        if overlay.is_symlink():
            raise ValueError(f'SDK overlay must not be a symlink: {overlay}')
        if overlay.exists():
            shutil.rmtree(overlay)
    if os.uname().sysname != 'Darwin':
        from .paths import config
        theos = Path(config().get('theos', str(Path.home() / 'theos'))).expanduser().resolve()
        require(theos / 'toolchain/linux/iphone/bin/ld', 'Linux Theos toolchain')
        link(theos / 'toolchain', DEPS / 'theos/toolchain')
        link(local_path('ios_sdk'), DEPS / 'theos/sdks/iPhoneOS16.5.sdk')
    with (DEPS / 'setup.log').open('w') as log:
        subprocess.run(['bash', str(HAKIOS / 'xnu-deps/setup.sh'), '--stage1'], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        save_json(sdk_marker, sdk_inputs)
        subprocess.run(['bash', str(HAKIOS / 'xnu-build.sh'), f'-j{jobs}', 'exporthdrs'], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        headers = Path(env['LILAC_OBJROOT']) / 'EXPORT_HDRS'
        key = fingerprint({'headers': inventory(headers), 'recipe': inventory(HAKIOS / 'xnu-deps'),
                           'configuration': env['LILAC_OBJROOT'], 'sdk': str(local_path('ios_sdk').resolve())})
        marker = DEPS / 'kernel-libraries.json'
        rebuild = not marker.exists() or json.loads(marker.read_text()).get('fingerprint') != key
        subprocess.run(['bash', str(HAKIOS / 'xnu-deps/setup.sh'), '--rebuild-libs' if rebuild else '--stage2'],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        save_json(marker, {'fingerprint': key})
        pkg_config = subprocess.run(['pkg-config', '--cflags', '--libs', 'openssl'], capture_output=True, text=True, check=True)
        subprocess.run(['bash', str(HAKIOS / 'tools/apfs/build-tool.sh')],
                       env={**env, 'TOOL_CFLAGS': pkg_config.stdout.strip()}, stdout=log, stderr=subprocess.STDOUT, check=True)
    save_json(DEPS / 'manifest.json', {'inputs': sdk_inputs,
                                     'libraries_fingerprint': key,
                                     'compiler': subprocess.check_output(['/usr/bin/clang', '--version'], text=True).strip(),
                                     'compiler_sha256': digest('/usr/bin/clang'),
                                     'linker_sha256': digest(DEPS / 'host/bin/ld64'),
                                     'input_provenance': json.loads((ROOT / 'local/input-provenance.json').read_text()) if (ROOT / 'local/input-provenance.json').is_file() else None,
                                     'host_tools': inventory(DEPS / 'host/bin')})
    print(f'Dependencies ready: {DEPS}')
