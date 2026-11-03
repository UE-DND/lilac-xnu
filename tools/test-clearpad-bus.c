/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/lilac-clearpad-bus.h"
struct fake { unsigned calls; int fail; };
static int xfer(void *context,uint8_t addr,const uint8_t *w,size_t wn,uint8_t *r,size_t rn) {
    struct fake *f=context; f->calls++; assert(addr==0x2c);
    if (f->calls==1) { assert(wn==2 && w[0]==0xff && w[1]==2 && !r && !rn); return f->fail==1?-1:0; }
    assert(f->calls==2 && wn==1 && w[0]==0x80 && rn==3);
    memset(r,0x34,rn); return f->fail==2?-1:0;
}
int main(void) {
    struct fake f={0}; struct lilac_clearpad_bus bus={xfer,&f}; uint8_t out[]={1,2,3};
    assert(!lilac_clearpad_read(&bus,0x280,out,3) && f.calls==2 && out[0]==0x34);
    for (int fail=1;fail<=2;fail++) { f=(struct fake){0,fail}; memset(out,0x55,3);
        assert(lilac_clearpad_read(&bus,0x280,out,3) && out[0]==0x55 && out[2]==0x55); }
    f=(struct fake){0}; assert(lilac_clearpad_read(&bus,0x2fe,out,3) && !f.calls);
    assert(lilac_clearpad_read(&bus,0x580,out,3) && !f.calls);
    puts("PASS: serialized RMI page selection, repeated-start contract and failed-read isolation");
}
