import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out'
XNU = ROOT / 'sources/xnu'
HAKIOS = ROOT / 'sources/hakios'
PEPARSE = ROOT / 'third_party/pe-parse'
SHARED = ROOT / 'shared'
DEPS = OUT / 'deps'
CONFIG_FILE = ROOT / 'local/config.json'

DEFAULTS = {
    'ios': 'local/ios/16.5-20F66',
    'ios_sdk': 'local/sdk/iPhoneOS16.5.sdk',
    'macos_sdk': 'local/sdk/MacOSX13.3.sdk',
    'dtb': 'local/readback-20261101/live.dtb',
    'system_map': 'configs/lilac/system-map.bin',
    'gpu_firmware': 'local/gpu-firmware',
    'sony_firmware': 'local/sony-firmware/firmware.tar',
    'llvm': '/opt/homebrew/opt/llvm/bin' if os.uname().sysname == 'Darwin' else '/usr/bin',
}


def config():
    if not CONFIG_FILE.exists():
        return {}
    value = json.loads(CONFIG_FILE.read_text())
    if not isinstance(value, dict) or any(not isinstance(item, str) for item in value.values()):
        raise ValueError('local/config.json must map input names to paths')
    return value


def local_path(name, explicit=None):
    value = explicit if explicit is not None else config().get(name, DEFAULTS[name])
    path = Path(value).expanduser()
    return (path if path.is_absolute() else ROOT / path).absolute()


def require(path, description=None):
    path = Path(path)
    if not path.exists():
        raise ValueError(f'missing {description or "input"}: {path}')
    return path


def load_lock():
    value = json.loads((ROOT / 'upstream.lock.json').read_text())
    if value.get('version') != 1:
        raise ValueError('unsupported upstream.lock.json version')
    return value


def environment():
    return {
        **os.environ,
        'PYTHONDONTWRITEBYTECODE': '1',
        'LILAC_ROOT': str(ROOT),
        'XNU_SRC': str(XNU),
        'XNUDEPS': str(DEPS),
        'LILAC_IOS_SDK': str(local_path('ios_sdk')),
        'LILAC_MACOS_SDK': str(local_path('macos_sdk')),
        'LILAC_SYSIMAGE': str(local_path('ios')),
        'LILAC_APFS_TOOL': str(OUT / 'deps/host/bin/apfs-tool'),
    }
