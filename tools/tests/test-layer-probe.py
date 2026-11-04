#!/usr/bin/env python3
"""Run the production CoreAnimation offscreen scene against macOS frameworks."""
from pathlib import Path
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
harness = '''
#define LILAC_UIKIT 0
#define main lilac_device_main
#include %s
#undef main
int main(void) {
    @autoreleasepool {
        IOSurfaceRef s=make_surface(); if(!s) return 1;
        int detail=0,scene=render_scene(s,&detail);
        CFRelease(s);
        printf("host CoreAnimation scene=%%d opacity=%%d\\n",scene,detail);
        return scene && detail ? 0 : 1;
    }
}
''' % json.dumps(str(root/'userland/layer-probe.m'))
with tempfile.TemporaryDirectory(prefix='lilac-layer-') as directory:
    path=Path(directory)
    (path/'test.m').write_text(harness)
    subprocess.run(['clang','-O1','-fobjc-arc','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    str(path/'test.m'),'-o',str(path/'test'),'-framework','Foundation','-framework','QuartzCore',
                    '-framework','CoreGraphics','-framework','IOSurface','-framework','IOKit'],check=True)
    subprocess.run([str(path/'test')],check=True)
