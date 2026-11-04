/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-a5xx-ring.h"
int main(void) {
    uint32_t memory[8], before[8], data[]={0x11,0x22,0x33,0x44,0x55,0x66,0x77};
    for(size_t w=0;w<8;w++)for(size_t r=0;r<8;r++)for(size_t n=0;n<=7;n++) {
        for(unsigned j=0;j<8;j++)memory[j]=before[j]=0xabcdef00+j;
        struct lilac_a5xx_ring ring={memory,8,w};size_t occupied=(w+8-r)%8;
        int allowed=n+1<=7-occupied;int ok=lilac_a5xx_ring_packet7(&ring,r,0x3d,data,n);
        assert(ok==allowed);
        if(!ok) {assert(ring.write_index==w && !memcmp(memory,before,sizeof(memory)));continue;}
        uint32_t linear[8];struct lilac_pm4 packet={linear,8,0};assert(lilac_pm4_type7(&packet,0x3d,data,n));
        for(size_t i=0;i<n+1;i++)assert(memory[(w+i)%8]==linear[i]);
        for(size_t i=n+1;i<8;i++)assert(memory[(w+i)%8]==before[(w+i)%8]);
        assert(ring.write_index==(w+n+1)%8);
    }
    struct lilac_a5xx_ring bad={memory,7,0};assert(!lilac_a5xx_ring_packet7(&bad,0,1,data,1));
    bad.count=8;bad.write_index=8;assert(!lilac_a5xx_ring_packet7(&bad,0,1,data,1));
    bad.write_index=0;assert(!lilac_a5xx_ring_packet7(&bad,8,1,data,1));
    assert(!lilac_a5xx_ring_packet7(&bad,0,0x80,data,1));
    puts("PASS: all 8-slot producer/consumer positions, wraps, full-ring rejection and no partial writes");
}
