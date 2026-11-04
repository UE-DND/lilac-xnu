#!/usr/bin/env python3
"""Run the production CoreGraphics/CoreText drawing checks with macOS frameworks."""
from pathlib import Path
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lilaclib.paths import local_path
font = local_path('ios') / 'rootfs/System/Library/Fonts/Core/CourierNew.ttf'
source = root / 'userland/graphics-probe.c'
if not font.is_file():
    raise SystemExit('missing extracted iOS CourierNew.ttf')
harness = '''
#define LILAC_GRAPHICS_FONT %s
#define main lilac_device_main
#include %s
#undef main
int main(void) {
    IOSurfaceRef surface=make_surface();
    if (!surface) return 1;
    int bitmap=bitmap_draw(surface), vector=vector_draw(), text=text_draw();
    CFRelease(surface);
    printf("host CoreGraphics bitmap=%%d vector=%%d CoreText=%%d\\n",bitmap,vector,text);
    return bitmap && vector && text ? 0 : 1;
}
''' % (json.dumps(str(font)), json.dumps(str(source)))
with tempfile.TemporaryDirectory(prefix='lilac-graphics-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['clang', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(path / 'test.c'), '-o', str(path / 'test'),
                    '-framework', 'CoreFoundation', '-framework', 'CoreGraphics', '-framework', 'CoreText',
                    '-framework', 'IOSurface', '-framework', 'IOKit'], check=True)
    subprocess.run([str(path / 'test')], check=True)
