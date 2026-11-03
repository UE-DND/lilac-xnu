/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include "../bootloader/smp-topology.h"
struct fixture { int64_t states[8];unsigned calls; };
static int64_t affinity(uint32_t id,void *arg) {
    struct fixture *f=arg;unsigned i=(id>>8)*4+(id&255);
    assert(i<8);f->calls++;return f->states[i];
}
int main(void) {
    struct fixture f={{1,1,1,1,1,1,1,1},0};uint32_t out[8];
    assert(lilac_smp_topology(0x80000000,affinity,&f,out)==8 && f.calls==7 && out[0]==0);
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<i;j++)assert(out[i]!=out[j]);
    f.calls=0;f.states[0]=0;f.states[1]=0;f.states[2]=2;f.states[3]=-1;
    assert(lilac_smp_topology(0x100,affinity,&f,out)==4 && f.calls==7 && out[0]==0x100);
    assert(out[1]==0x101 && out[2]==0x102 && out[3]==0x103);
    f.calls=0;assert(lilac_smp_topology(0x200,affinity,&f,out)==1 && f.calls==0 && out[0]==0x200);
    assert(lilac_smp_topology(3,NULL,NULL,out)==1 && out[0]==3);
    puts("PASS: verified MPIDR ordering; only PSCI OFF cores; unsupported, pending and unknown fail closed");
}
