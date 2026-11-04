#!/usr/bin/env python3
"""Exercise entry-point readiness while another CPU is finishing GPU resume."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'sources/xnu/iokit/Kernel/hakios.gpu.cpp').read_text()
start = source.index('static bool\ngpuReady(void)')
end = source.index('\n}', start) + 2
ready = source[start:end]
# Both of these are entered without the service lock. All other readiness
# queries belong to existing locked driver operations.
for entry, stop in [('HakiosGPUUserClient::start(', 'fBuffers ='),
                    ('HakiosGPUUserClient::externalMethod(', 'bzero(&r')]:
    body = source[source.index(entry):]
    body = body[:body.index(stop)]
    assert 'gpuReady()' in body and 'hakios_gpu_ready()' not in body

program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static int lock_object;
static int *gGPULock = &lock_object;
static bool gpu_kept, resume_pending, outcome, locked;
static unsigned waited;
static void IOLockLock(int *lock) {
    assert(lock == gGPULock && !locked);
    /* The resume worker already owns the real service lock: acquiring it
     * waits for that worker to publish the final ownership state. */
    if (resume_pending) {
        waited++;
        gpu_kept = outcome;
        resume_pending = false;
    }
    locked = true;
}
static void IOLockUnlock(int *lock) {
    assert(lock == gGPULock && locked);
    locked = false;
}
static bool hakios_gpu_ready(void) {
    assert(locked);
    return gpu_kept;
}
''' + ready + r'''
int main(void) {
    /* GEM_NEW arrives during the temporary false state of resume. */
    gpu_kept = false; resume_pending = true; outcome = true;
    assert(gpuReady() && waited == 1 && !locked);
    /* An actual final stop must still reject new clients and requests. */
    gpu_kept = true; resume_pending = true; outcome = false;
    assert(!gpuReady() && waited == 2 && !locked);
    assert(!gpuReady() && !locked);
    gpu_kept = true;
    assert(gpuReady() && !locked);
    puts("PASS: entry-point readiness waits for resume; final stop remains unavailable");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    test = directory / 'ready.c'
    binary = directory / 'ready'
    test.write_text(program)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
