import os
from pathlib import Path
import subprocess
import sys

from .paths import ROOT, local_path, environment
from .records import run_directory, save_json

INTEGRATION_PYTHON = {'a540-firmware', 'graphics-probe', 'layer-probe', 'fast-boot'}


def run(group, build_path=None):
    output = run_directory('test-' + group)
    output.mkdir(parents=True)
    results = []
    tests = []
    for path in sorted((ROOT / 'tools/tests').glob('test-*.py')):
        name = path.stem[5:]
        if (name in INTEGRATION_PYTHON) != (group == 'integration'):
            continue
        command = [sys.executable, str(path)]
        missing = None
        if name == 'fast-boot':
            if not build_path:
                missing = 'needs --build PATH to a fast-boot userspace artifact'
            else:
                command.append(str(Path(build_path).resolve()))
        if name in ('graphics-probe', 'layer-probe') and sys.platform != 'darwin':
            missing = 'requires macOS frameworks'
        elif name == 'graphics-probe' and not (local_path('ios') / 'rootfs/System/Library/Fonts/Core/CourierNew.ttf').is_file():
            missing = 'missing iOS system CourierNew.ttf; register ios in local/config.json'
        if name == 'a540-firmware':
            from tools import a540_firmware
            absent = [filename for filename in a540_firmware.FILES if not (local_path('gpu_firmware') / filename).is_file()]
            if absent:
                missing = 'missing GPU firmware: ' + ', '.join(absent)
        tests.append((name, command, missing))
    for path in sorted((ROOT / 'tools/tests').glob('test-*.c')):
        name = path.stem[5:]
        if (name == 'a540-gpmu') != (group == 'integration'):
            continue
        binary = output / name
        command = ['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(path), '-o', str(binary)]
        with (output / (name + '-compile.log')).open('w') as log:
            status = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT).returncode
        if status:
            results.append({'test': name, 'status': 'failed', 'phase': 'compile', 'exit': status})
            continue
        invocation = [str(binary)]
        if name == 'a540-gpmu':
            invocation.append(str(local_path('gpu_firmware') / 'a540_gpmu.fw2'))
        missing = 'missing a540_gpmu.fw2' if name == 'a540-gpmu' and not Path(invocation[-1]).is_file() else None
        tests.append((name, invocation, missing))
    for name, command, missing in tests:
        if missing:
            result = {'test': name, 'status': 'skipped', 'reason': missing}
        else:
            with (output / (name + '.log')).open('w') as log:
                status = subprocess.run(command, cwd=ROOT, env=environment(), stdout=log, stderr=subprocess.STDOUT).returncode
            result = {'test': name, 'status': 'passed' if status == 0 else 'failed', 'exit': status}
        results.append(result)
        print(f'{result["status"].upper()}: {name}', flush=True)
    save_json(output / 'results.json', {'group': group, 'tests': results})
    print(f'Test report: {output}')
    return 1 if any(item['status'] != 'passed' for item in results) else 0
