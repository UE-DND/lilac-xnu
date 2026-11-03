#!/usr/bin/env python3
"""Exercise the production panic-text range selection with synthetic headers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
from lilaclib.paths import XNU
source = XNU / 'osfmk/arm64/loader_return.c'
text = source.read_text()
start = text.index('static void\nloader_log_append_panic_text(void)')
end = text.index('\nvoid\nloader_return(void)', start)
function = text[start:end]
program = r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define EMBEDDED_PANIC_MAGIC 0x46554e4b
/* Only fields consumed by the production selector are needed by this test. */
struct header { uint32_t eph_magic, eph_panic_log_offset, eph_panic_log_len, eph_stackshot_offset; };
static struct header h;
static struct header *panic_info;
static unsigned char region[256];
static uintptr_t gPanicBase;
static unsigned int gPanicSize;
static char output[512];
static size_t used;
static void loader_log_putc(char c) { assert(used < sizeof(output)); output[used++] = c; }
static void loader_log_puts(const char *s) { while (*s) loader_log_putc(*s++); }
''' + function + r'''
static void setup(void) {
    memset(region, 0xa5, sizeof(region));
    memcpy(region + 64, "panic(cpu 0): example\n", sizeof("panic(cpu 0): example\n") - 1);
    memset(output, 0, sizeof(output));
    used = 0;
    h = (struct header){EMBEDDED_PANIC_MAGIC, 64, sizeof("panic(cpu 0): example\n") - 1, 96};
    panic_info = &h;
    gPanicBase = (uintptr_t)region;
    gPanicSize = sizeof(region);
}
static void empty(void) { loader_log_append_panic_text(); assert(used == 0); }
int main(void) {
    setup(); loader_log_append_panic_text();
    assert(strcmp(output, "\n--- panic text ---\npanic(cpu 0): example\n") == 0);
    assert(used == strlen(output));
    setup(); h.eph_panic_log_len = 33; empty(); /* would consume stackshot */
    setup(); h.eph_stackshot_offset = 32; empty();
    setup(); h.eph_panic_log_offset = 257; empty();
    setup(); h.eph_panic_log_len = UINT32_MAX; empty();
    setup(); h.eph_panic_log_len = 0; empty();
    setup(); h.eph_panic_log_offset = 0; empty();
    setup(); h.eph_magic = 0; empty();
    setup(); panic_info = NULL; empty();
    puts("PASS: panic text only, excludes stackshot, rejects invalid ranges");
}
'''
with tempfile.TemporaryDirectory(prefix='lilac-panic-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(program)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
