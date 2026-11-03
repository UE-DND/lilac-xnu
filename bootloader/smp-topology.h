/* SPDX-License-Identifier: BSD-2-Clause
 * MSM8998 MPIDRs from the verified Sony device tree. Only lend the kernel
 * cores that firmware explicitly reports OFF; the running core always comes
 * first. An unknown boot core or unsupported affinity query fails closed.
 */
#ifndef LILAC_SMP_TOPOLOGY_H
#define LILAC_SMP_TOPOLOGY_H
#include <stdint.h>
#define LILAC_SMP_MAX_CPUS 8u
typedef int64_t (*lilac_affinity_fn)(uint32_t mpidr, void *context);
static inline unsigned lilac_smp_topology(uint32_t boot, lilac_affinity_fn affinity,
                                         void *context, uint32_t out[LILAC_SMP_MAX_CPUS]) {
    static const uint32_t mpidrs[LILAC_SMP_MAX_CPUS]={0,1,2,3,0x100,0x101,0x102,0x103};
    unsigned boot_index=LILAC_SMP_MAX_CPUS, count=1;
    boot &= 0xffffu;out[0]=boot;
    for(unsigned i=0;i<LILAC_SMP_MAX_CPUS;i++)if(mpidrs[i]==boot)boot_index=i;
    if(boot_index==LILAC_SMP_MAX_CPUS || !affinity)return count;
    for(unsigned distance=1;distance<LILAC_SMP_MAX_CPUS;distance++) {
        unsigned candidates[2]={boot_index+distance,boot_index>=distance?boot_index-distance:LILAC_SMP_MAX_CPUS};
        for(unsigned side=0;side<2;side++) {
            unsigned index=candidates[side];
            if(index<LILAC_SMP_MAX_CPUS && affinity(mpidrs[index],context)==1)out[count++]=mpidrs[index];
        }
    }
    return count;
}
#endif
