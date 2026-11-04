/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-a540-power.h"
struct fake { uint32_t reg[0x2000/4]; unsigned operations, fail; int apply_failure, stuck_gx; };
static int read32(void *context, uint32_t offset, uint32_t *value)
{
    struct fake *f=context; assert(offset<0x2000 && !(offset&3));
    if (++f->operations==f->fail) return 0;
    *value=f->reg[offset/4]; return 1;
}
static int write32(void *context, uint32_t offset, uint32_t value)
{
    struct fake *f=context; assert(offset<0x2000 && !(offset&3));
    int fail=++f->operations==f->fail;
    if (fail && !f->apply_failure) return 0;
    if (offset==0x1094) {
        if (!(value&1)) {
            assert((f->reg[0x1070/4]&2) && !(f->reg[0x130/4]&17) && !f->reg[0x1090/4]);
            value|=0x80000000;
        } else if (!f->stuck_gx) value&=~0x80000000u;
    }
    if (offset==0x1098 || offset==0x10d0) {
        if (value&1) { assert(f->reg[0x1094/4]&0x80000000); value&=~0x80000000u; }
        else value|=0x80000000;
    }
    if (offset==0x1070) {
        value&=~1u; /* RCG UPDATE may clear before the first read */
        if (value&2) value&=~0x80000000u;
        else { assert(!(f->reg[0x1094/4]&0x80000000)); value|=0x80000000; }
    }
    if (offset==0x10b0) value&=~1u;
    f->reg[offset/4]=value;
    return !fail;
}
static void delay(void *context,uint32_t us) { (void)context; assert(us==1); }
static struct fake initial(void)
{
    struct fake f={0};
    f.reg[0x1020/4]=1; f.reg[0x1094/4]=0x222001; f.reg[0x1098/4]=0x80000620;
    f.reg[0x10d0/4]=f.reg[0x1070/4]=f.reg[0x10b0/4]=0x80000000; f.reg[0x130/4]=1;
    return f;
}
int main(void)
{
    struct fake f=initial(), original=f;
    struct lilac_a540_power p;
    const struct lilac_a540_power_io io={&f,read32,write32,delay};
    assert(lilac_a540_power_acquire(&io,&p) && p.ready);
    unsigned acquire_ops=f.operations;
    assert(lilac_a540_power_release(&io,&p) && !p.touched);
    assert(!memcmp(f.reg,original.reg,sizeof(f.reg)));
    for (int applied=0;applied<=1;applied++) for (unsigned failure=1;failure<=acquire_ops;failure++) {
        f=initial(); f.fail=failure; f.apply_failure=applied;
        assert(!lilac_a540_power_acquire(&io,&p));
        assert(lilac_a540_power_release(&io,&p));
        assert(!memcmp(f.reg,original.reg,sizeof(f.reg)));
    }
    f=initial(); assert(lilac_a540_power_acquire(&io,&p)); f.stuck_gx=1;
    assert(!lilac_a540_power_release(&io,&p) && p.touched && (f.reg[0x1070/4]&2));
    assert(!(f.reg[0x130/4]&1));
    f=initial(); f.reg[0x1094/4]|=0x80000000;
    assert(!lilac_a540_power_acquire(&io,&p) && !p.touched);
    puts("PASS: A540 GX ordering, self-clearing RCG, failure before/after writes and retained upstream power on timeout");
}
