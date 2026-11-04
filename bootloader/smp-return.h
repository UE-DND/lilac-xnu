/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LILAC_SMP_RETURN_H
#define LILAC_SMP_RETURN_H
#include "smp-topology.h"

enum lilac_smp_return_result {
    LILAC_SMP_RETURN_OK,
    LILAC_SMP_RETURN_BAD_TOPOLOGY,
    LILAC_SMP_RETURN_WRONG_CPU,
    LILAC_SMP_RETURN_NOT_OFF,
};

/* Take one complete snapshot, including failures. Never interpret a PSCI
 * error or ON_PENDING as OFF, or skip the returning core if it is a borrower.
 * The caller handles count == 0 only before any topology has been lent. */
static inline enum lilac_smp_return_result
lilac_smp_check_return(const uint32_t *mpidrs, unsigned count, uint32_t current,
                      lilac_affinity_fn affinity, void *context,
                      int64_t states[LILAC_SMP_MAX_CPUS]) {
    if (!mpidrs || !affinity || count == 0 || count > LILAC_SMP_MAX_CPUS)
        return LILAC_SMP_RETURN_BAD_TOPOLOGY;
    for (unsigned i = 0; i < count; i++) {
        if (mpidrs[i] & ~0xffffu) return LILAC_SMP_RETURN_BAD_TOPOLOGY;
        for (unsigned j = 0; j < i; j++)
            if (mpidrs[i] == mpidrs[j]) return LILAC_SMP_RETURN_BAD_TOPOLOGY;
    }
    int all_off = 1;
    for (unsigned i = 1; i < count; i++) {
        states[i] = affinity(mpidrs[i], context);
        if (states[i] != 1) all_off = 0;
    }
    if ((current & 0xffffu) != mpidrs[0]) return LILAC_SMP_RETURN_WRONG_CPU;
    return all_off ? LILAC_SMP_RETURN_OK : LILAC_SMP_RETURN_NOT_OFF;
}
#endif
