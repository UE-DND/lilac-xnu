/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/lilac-clearpad-f12.h"
int main(void) {
    uint8_t presence[2] = {0x8a, 0x80}, offsets[16];
    assert(lilac_f12_offsets(presence,2,offsets,16));
    assert(offsets[0]==0xff && offsets[1]==0 && offsets[3]==1 && offsets[7]==2 && offsets[15]==3);
    assert(!lilac_f12_offsets(presence,2,offsets,17));
    struct lilac_f12_state s = {0}, before;
    struct lilac_f12_event e[10]; size_t n = 99;
    uint8_t f[80] = {0};
    f[0]=1; f[1]=0xcf; f[2]=2; f[3]=0xff; f[4]=4; f[5]=42;
    f[16]=1; f[17]=10; f[19]=20; f[21]=9;
    assert(lilac_f12_frame(&s,5,f,24,e,10,&n) && n==2);
    assert(e[0].kind==LILAC_TOUCH_DOWN && e[0].x==719 && e[0].y==1279);
    assert(e[1].slot==2 && e[1].kind==LILAC_TOUCH_DOWN);
    assert(lilac_f12_frame(&s,5,f,24,e,10,&n) && n==0);
    f[17]=11;
    assert(lilac_f12_frame(&s,5,f,24,e,10,&n) && n==1 && e[0].kind==LILAC_TOUCH_MOVE);
    before=s; n=99; f[17]=12;
    assert(!lilac_f12_frame(&s,5,f,24,e,0,&n) && n==99 && !memcmp(&s,&before,sizeof s));
    f[0]=9; assert(!lilac_f12_frame(&s,5,f,24,e,10,&n) && !memcmp(&s,&before,sizeof s));
    f[0]=1; f[1]=0xd0; assert(!lilac_f12_frame(&s,5,f,24,e,10,&n));
    assert(!lilac_f12_frame(&s,0x400,f,24,e,10,&n));
    assert(!lilac_f12_frame(&s,5,f,23,e,10,&n));
    assert(lilac_f12_frame(&s,0,NULL,0,e,10,&n) && n==2 && e[0].kind==LILAC_TOUCH_UP && e[1].slot==2);
    puts("PASS: S3330 F12 frames, multi-touch transitions and malformed-frame rejection");
}
