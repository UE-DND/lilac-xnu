/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/lilac-a5xx-pm4.h"
static unsigned reference_parity(uint32_t x) { unsigned ones=0;while(x){ones+=x&1;x>>=1;}return !(ones&1); }
int main(void) {
    uint32_t mem[16400],payload[16383];memset(payload,0x5a,sizeof(payload));
    struct lilac_pm4 b={mem,16400,0};
    for(unsigned reg=0;reg<0x40000;reg++) {
        b.used=0;assert(lilac_pm4_type4(&b,reg,payload,1));
        assert(mem[0]==(0x40000000u|1u|(reference_parity(1)<<7)|(reg<<8)|(reference_parity(reg)<<27)));
        assert(b.used==2 && mem[1]==payload[0]);
    }
    for(unsigned n=0;n<0x4000;n++) {
        unsigned op=n&0x7f;b.used=0;assert(lilac_pm4_type7(&b,op,payload,n));
        assert(mem[0]==(0x70000000u|n|(reference_parity(n)<<15)|(op<<16)|(reference_parity(op)<<23)));
        assert(b.used==n+1 && (!n || mem[n]==payload[n-1]));
    }
    b.used=0;b.capacity=1;mem[0]=0xdeadbeef;
    assert(!lilac_pm4_type4(&b,1,payload,1) && b.used==0 && mem[0]==0xdeadbeef);
    assert(!lilac_pm4_type7(&b,1,payload,1) && b.used==0 && mem[0]==0xdeadbeef);
    assert(!lilac_pm4_type4(&b,0x40000,payload,1));
    assert(!lilac_pm4_type4(&b,1,payload,128));
    assert(!lilac_pm4_type7(&b,128,NULL,0));
    assert(!lilac_pm4_type7(&b,1,payload,0x4000));
    assert(!lilac_pm4_type7(&b,1,NULL,1));
    b.used=2;assert(!lilac_pm4_type7(&b,1,NULL,0));
    puts("PASS: A5xx PM4 packet headers, parity, all field values, bounds and failure atomicity");
}
