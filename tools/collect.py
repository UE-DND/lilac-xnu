#!/usr/bin/env python3
"""Read-only ADB inventory. Never invoke su, exploit binaries, reboot or flash."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    args = parser.parse_args()
    out = ROOT / 'local' / 'runs' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    out.mkdir(parents=True, exist_ok=False)
    commands = {
        'identity': 'getprop ro.product.model; getprop ro.board.platform; getprop ro.build.display.id; getprop ro.boot.flash.locked; getprop ro.boot.verifiedbootstate; id; uname -a',
        'memory': 'cat /proc/meminfo; cat /proc/cpuinfo',
        'input': 'cat /proc/bus/input/devices; wm size; wm density',
        'storage': 'ls -l /dev/block/bootdevice/by-name; df -h /data',
        'dtb-access': 'ls -l /sys/firmware/fdt /data/local/tmp/fdt-3token.bin; command -v su',
    }
    manifest = {'time': datetime.datetime.now().isoformat(), 'commands': {}}
    for name, command in commands.items():
        result = subprocess.run([args.adb, 'shell', command], capture_output=True, timeout=30)
        data = result.stdout + result.stderr
        (out / (name + '.txt')).write_bytes(data)
        manifest['commands'][name] = {'command': command, 'exit': result.returncode, 'sha256': hashlib.sha256(data).hexdigest()}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(out)


if __name__ == '__main__':
    main()
