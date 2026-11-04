/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-a540-me.h"
int main(void) {
    uint32_t words[16]={0}, before[16];
    struct lilac_a5xx_ring r={words,16,12};
    const uint32_t payload[8]={0x2f,3,0x20000000,0,0,0,0,0};
    assert(lilac_a540_me_init(&r,8) && r.write_index==5);
    assert((words[12]&0x7f)==8 && ((words[12]>>16)&0x7f)==0x48);
    for(size_t i=0;i<8;i++) assert(words[(13+i)&15]==payload[i]);
    memcpy(before,words,sizeof words); size_t at=r.write_index;
    assert(!lilac_a540_me_init(&r,6) && r.write_index==at && !memcmp(before,words,sizeof words));
    puts("PASS: A540 CP_ME_INIT words, wraparound and no-space rejection");
}
