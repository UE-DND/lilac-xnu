"""Small userland programs shared by RAM and UFS image builders."""
from pathlib import Path
import subprocess

from .paths import HAKIOS, DEPS, environment


def dynamic_probe_inputs():
    return [Path(__file__).resolve(), HAKIOS / 'xnu-env.sh',
            HAKIOS / 'userland/src/hello_dyn.c',
            DEPS / 'host/bin/xnu-clang', DEPS / 'host/bin/ld64']


def build_dynamic_probe(output, log):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['bash', '-c', '''set -eu
source "$1"
shift
exec xnu-clang -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" "$@"
''', 'lilac-dynamic-build', str(HAKIOS / 'xnu-env.sh'),
                    '-arch', 'arm64', '-march=armv8-a', '-O1',
                    str(HAKIOS / 'userland/src/hello_dyn.c'), '-o', str(output)],
                   env=environment(), stdout=log, stderr=subprocess.STDOUT, check=True)
