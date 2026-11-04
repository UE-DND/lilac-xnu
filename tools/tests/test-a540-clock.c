/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-a540-clock.h"
struct fake { uint32_t r[0x2000/4]; unsigned ops, fail; int stuck; };
static int rd(void *p,uint32_t off,uint32_t *v) {
    struct fake *f=p; if (++f->ops==f->fail) return 0; *v=f->r[off/4];return 1;
}
static int wr(void *p,uint32_t off,uint32_t v) {
    struct fake *f=p; if (++f->ops==f->fail) return 0;
    if (off==0x1070) v&=~1u;
    if (off==4 || off==8 || off==0x38) assert(!f->r[0x1074/4] && !f->r[0x2c/4] && !(f->r[0]&1));
    if (off==0x1074 && v) assert((f->r[0]&0x80000001)==0x80000001);
    f->r[off/4]=v;
    if (off==0x2c) { if (v && !f->stuck) f->r[0]|=0x80000000; else f->r[0]&=~0x80000000u; }
    return 1;
}
static void wait_us(void *p,uint32_t us) {(void)p;assert(us==1);}
int main(void) {
    struct fake f={0}; struct lilac_a540_clock c={0};
    struct lilac_a540_power_io io={&f,rd,wr,wait_us};
    assert(!lilac_a540_clock_set(&io,&c,710000000) && !f.ops);
    assert(lilac_a540_clock_set(&io,&c,180000000)); unsigned operations=f.ops;
    assert(c.hz==180000000 && f.r[4/4]==18 && f.r[0x38/4]==49152);
    assert(lilac_a540_clock_set(&io,&c,257000000) && c.hz==257000000);
    assert(lilac_a540_clock_restore(&io,&c) && !c.owned);
    for (unsigned fail=1;fail<=operations;fail++) {
        memset(&f,0,sizeof(f)); memset(&c,0,sizeof(c)); f.fail=fail;
        assert(!lilac_a540_clock_set(&io,&c,180000000));
        assert(lilac_a540_clock_restore(&io,&c));
        assert(!(f.r[0]&1) && !f.r[0x2c/4] && !f.r[0x1074/4]);
    }
    memset(&f,0,sizeof(f)); memset(&c,0,sizeof(c)); f.stuck=1;
    assert(!lilac_a540_clock_set(&io,&c,180000000));
    assert(!(f.r[0]&1) && !f.r[0x1074/4]);
    puts("PASS: bounded Fabia transitions, unsupported OPP rejection, XO-before-retune, lock failure and partial-write cleanup");
}
