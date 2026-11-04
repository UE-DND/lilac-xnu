/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-a5xx-submit.h"

int main(void)
{
    uint32_t memory[32], before[32], expected[16];
    const struct lilac_a5xx_ib ibs[] = {{0x1234000, 7}, {0x2abcd0000ull, 29}};
    struct lilac_pm4 packet = {expected, 16, 0};
    const uint32_t a[] = {0x1234000, 0, 7}, b[] = {0xabcd0000, 2, 29};
    const uint32_t end[] = {4, 0x1000, 0, 9};
    assert(lilac_pm4_type7(&packet, 0x3f, a, 3));
    assert(lilac_pm4_type7(&packet, 0x3f, b, 3));
    assert(lilac_pm4_type7(&packet, 0x46, end, 4) && packet.used == 13);
    for (size_t w = 0; w < 32; w++) for (size_t r = 0; r < 32; r++) {
        for (unsigned i = 0; i < 32; i++) memory[i] = before[i] = 0xfeed0000u + i;
        struct lilac_a5xx_ring ring = {memory, 32, w};
        int enough = ((r - w - 1) & 31) >= packet.used;
        assert(lilac_a5xx_submit(&ring, r, ibs, 2, 0x1000, 9) == enough);
        if (!enough) { assert(ring.write_index == w && !memcmp(memory, before, sizeof(memory))); continue; }
        for (size_t i = 0; i < packet.used; i++) assert(memory[(w+i)&31] == expected[i]);
        for (size_t i = packet.used; i < 32; i++) assert(memory[(w+i)&31] == before[(w+i)&31]);
        assert(ring.write_index == ((w + packet.used) & 31));
    }
    struct lilac_a5xx_ring ring = {memory, 32, 0};
    memcpy(before, memory, sizeof(memory));
    assert(!lilac_a5xx_submit(&ring, 0, ibs, 2, 0x1001, 9));
    assert(!lilac_a5xx_submit(&ring, 0, ibs, 2, 0x1000, 0));
    struct lilac_a5xx_ib invalid = {LILAC_A5XX_VA_LIMIT - 4, 2};
    assert(!lilac_a5xx_submit(&ring, 0, &invalid, 1, 0x1000, 9));
    assert(ring.write_index == 0 && !memcmp(memory, before, sizeof(memory)));
    for (size_t w=0;w<32;w++) for (size_t r=0;r<32;r++) {
        for (unsigned i=0;i<32;i++) memory[i]=before[i]=0xfeed0000+i;
        ring=(struct lilac_a5xx_ring){memory,32,w};
        int enough=((r-w-1)&31)>=29;
        assert(lilac_a5xx_submit_mode(&ring,r,ibs,2,0x1000,9,1)==enough);
        if (!enough) { assert(ring.write_index==w && !memcmp(memory,before,sizeof(memory))); continue; }
        /* No privilege around either IB. The tail restores APRIV=0 and
         * protected mode before another client's commands can follow. */
        assert(memory[(w+1)&31]==ibs[0].address && memory[(w+5)&31]==(uint32_t)ibs[1].address);
        assert(memory[(w+26)&31]==0 && memory[(w+28)&31]==1);
        assert(ring.write_index==((w+29)&31));
    }
    puts("PASS: A5xx indirect submissions, fence packets, ring wraps and atomic rejection");
}
