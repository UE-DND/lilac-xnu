/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include "../../bootloader/smp-return.h"

struct fixture { int64_t state[8]; unsigned calls; };
static int64_t affinity(uint32_t mpidr, void *context) {
    struct fixture *f = context;
    unsigned index = (mpidr >> 8) * 4 + (mpidr & 255);
    assert(index < 8);
    f->calls++;
    return f->state[index];
}

int main(void) {
    const uint32_t cpus[] = {0x100, 0, 1, 2, 3, 0x101, 0x102, 0x103};
    struct fixture f = {{1, 1, 1, 1, 0, 1, 1, 1}, 0};
    int64_t states[8] = {0};
    assert(lilac_smp_check_return(cpus, 8, 0x80000100, affinity, &f, states) == LILAC_SMP_RETURN_OK);
    assert(f.calls == 7);
    for (unsigned i = 1; i < 8; i++) assert(states[i] == 1);
    /* Every borrower can individually block return. Complete the snapshot
     * even when the first query fails, so the screen diagnoses all CPUs. */
    const int64_t failures[] = {0, 2, -1, -2, -3, 0xffffffffu, 3};
    for (unsigned cpu = 0; cpu < 8; cpu++) {
        if (cpu == 4) continue;
        for (unsigned j = 0; j < sizeof(failures) / sizeof(failures[0]); j++) {
            f.state[cpu] = failures[j]; f.calls = 0;
            assert(lilac_smp_check_return(cpus, 8, 0x100, affinity, &f, states) == LILAC_SMP_RETURN_NOT_OFF);
            assert(f.calls == 7);
        }
        f.state[cpu] = 1;
    }
    assert(lilac_smp_check_return(cpus, 8, 0, affinity, &f, states) == LILAC_SMP_RETURN_WRONG_CPU);
    f.calls = 0;
    assert(lilac_smp_check_return(cpus, 1, 0x100, affinity, &f, states) == LILAC_SMP_RETURN_OK);
    assert(f.calls == 0);
    assert(lilac_smp_check_return(cpus, 0, 0x100, affinity, &f, states) == LILAC_SMP_RETURN_BAD_TOPOLOGY);
    assert(lilac_smp_check_return(cpus, 9, 0x100, affinity, &f, states) == LILAC_SMP_RETURN_BAD_TOPOLOGY);
    const uint32_t duplicate[] = {0x100, 0x100};
    const uint32_t malformed[] = {0x100, 0x10000};
    assert(lilac_smp_check_return(duplicate, 2, 0x100, affinity, &f, states) == LILAC_SMP_RETURN_BAD_TOPOLOGY);
    assert(lilac_smp_check_return(malformed, 2, 0x100, affinity, &f, states) == LILAC_SMP_RETURN_BAD_TOPOLOGY);
    assert(lilac_smp_check_return(cpus, 8, 0x100, NULL, &f, states) == LILAC_SMP_RETURN_BAD_TOPOLOGY);
    assert(f.calls == 0);
    puts("PASS: all borrowed CPUs must be OFF; wrong CPU, PSCI errors and corrupt topology refuse return");
}
