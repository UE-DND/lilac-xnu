#!/usr/bin/env python3
"""Exercise the actual display probe's asynchronous sample/settle checks."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'userland/display-probe.c').read_text()
function = source[source.index('static int wait_grid('):source.index('static int open_display(')]
harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static uint64_t info[16];
static char grid_failure[96];
static const uint32_t palette[9]={0xffef5350,0xff66bb6a,0xff42a5f5,
    0xff26c6da,0xffab47bc,0xffffca28,0xff78909c,0xffeeeeee,0xff101820};
static unsigned scenario, calls;
static unsigned settled_count(void) { return (uint32_t)info[6]; }
static int usleep(unsigned ignored) { (void)ignored; return 0; }
static int status(void) {
    calls++;
    if (scenario != 1 && calls >= 3) info[1] = 11;
    if (scenario != 2 && calls >= 5) info[6] = 0; /* 32-bit counter wrap */
    for (unsigned i=0;i<9;i++) info[7+i]=palette[i]&0xffffff;
    if (scenario == 3) info[10]=0x123456;
    if (scenario == 4) for (unsigned i=0;i<9;i++) info[7+i]=0;
    return 1;
}
'''
tests = r'''
int main(void) {
    scenario=0; calls=0; info[6]=0xffffffff; assert(wait_grid(10,0xffffffff,0,1) && calls==5);
    scenario=1; calls=0; info[1]=10; assert(!wait_grid(10,0xffffffff,0,0));
    assert(strstr(grid_failure,"no presented frame"));
    scenario=2; calls=0; info[1]=10; info[6]=0xffffffff; assert(!wait_grid(10,0xffffffff,0,1));
    assert(strstr(grid_failure,"no filtered redraw"));
    scenario=3; calls=0; info[1]=10; assert(!wait_grid(10,0xffffffff,0,0));
    assert(strstr(grid_failure,"sample 3 got 123456 expected 26c6da"));
    scenario=4; calls=0; info[1]=10; assert(wait_grid(10,0xffffffff,1,0));
    puts("PASS: actual asynchronous display checks, redraw counter wrap, timeout and RGB diagnostics");
}
'''
with tempfile.TemporaryDirectory(prefix='lilac-display-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness + function + tests)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
