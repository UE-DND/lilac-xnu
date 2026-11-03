#!/usr/bin/env python3
"""Host regression for the production pager range guard, without booting a device."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
from lilaclib.paths import XNU
DEFAULT_SOURCE = XNU / 'osfmk/vm/vm_object.c'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    text = args.source.read_text()
    start = text.index('kern_return_t\npager_map_to_phys_contiguous(')
    end = text.index('\nuint32_t scan_object_collision', start)
    function = text[start:end]
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int kern_return_t;
typedef bool boolean_t;
typedef uint32_t ppnum_t;
typedef uint64_t memory_object_offset_t, addr64_t, vm_size_t;
struct object { boolean_t private; };
typedef struct object *vm_object_t;
typedef struct object *memory_object_control_t;
#define TRUE true
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_FAILURE 5
static uint64_t page_size;
#define PAGE_MASK (page_size - 1)
static void *kernel_pmap;
static unsigned translations, populations;
static ppnum_t pmap_find_phys(void *map, addr64_t base) {
    (void)map; (void)base; translations++; return 0xa42c0;
}
static vm_object_t memory_object_control_to_vm_object(memory_object_control_t o) { return o; }
static void vm_object_lock(vm_object_t o) { (void)o; }
static void vm_object_unlock(vm_object_t o) { (void)o; }
static kern_return_t vm_object_populate_with_private(vm_object_t o,
    memory_object_offset_t offset, ppnum_t page, vm_size_t size) {
    (void)page;
    assert(o->private);
    /* The real implementation subtracts page_size until size == 0. */
    assert(!(size & PAGE_MASK) && !(offset & PAGE_MASK));
    populations++;
    return KERN_SUCCESS;
}
''' + function + r'''
static void check(uint64_t base, uint64_t offset, uint64_t size, bool valid) {
    struct object o = {false};
    translations = populations = 0;
    int result = pager_map_to_phys_contiguous(&o, offset, base, size);
    if (valid) {
        assert(result == KERN_SUCCESS && translations == 1 && populations == 1 && o.private);
    } else {
        assert(result == KERN_INVALID_ARGUMENT && translations == 0 && populations == 0 && !o.private);
    }
}
int main(void) {
    for (page_size = 4096; page_size <= 16384; page_size *= 4) {
        check(0xa42c0000, 0, page_size, true);
        check(0xa42c0000, page_size, 2 * page_size, true);
        check(0xa42c0001, 0, page_size, false);
        check(0xa42c0000, 1, page_size, false);
        check(0xa42c0000, 0, page_size - 1, false);
        check(0xa42c0000, 0, page_size + 1, false);
        /* The old hello RAMDisk: valid for 4KiB, unsafe for 16KiB VM pages. */
        check(0xa42c0000, 0, 20480, page_size == 4096);
        check(0xa42c0000, 0, 32768, true);
    }
    puts("PASS: production pager guard, 4KiB/16KiB, old 20KiB and new 32KiB RAMDisk");
}
'''
    with tempfile.TemporaryDirectory(prefix='lilac-pager-') as directory:
        source = Path(directory) / 'pager.c'
        binary = Path(directory) / 'pager-test'
        source.write_text(harness)
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
