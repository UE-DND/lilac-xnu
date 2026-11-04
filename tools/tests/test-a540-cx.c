/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include "../../kernel/lilac-a540-cx.h"

struct fake { uint32_t reg, status, writes, delay, powered; int no_on, no_vote, no_restore, keep; };
static uint32_t read_reg(void *p, uint32_t offset)
{
    struct fake *f = p;
    assert(offset == LILAC_A540_CX_GDSCR || offset == LILAC_A540_CX_HW_STATUS);
    return offset == LILAC_A540_CX_GDSCR ? f->reg : f->status;
}
static void write_reg(void *p, uint32_t offset, uint32_t v)
{
    struct fake *f = p; assert(offset == LILAC_A540_CX_GDSCR); f->writes++;
    if (!(v & 1) && f->no_vote) return;
    if ((v & 1) && f->no_restore) return;
    f->reg = v;
    if (!(v & 1) && !f->no_on) f->status |= LILAC_A540_CX_POWER_ON;
    if (v & 1) f->status &= ~LILAC_A540_CX_POWER_ON;
}
static void delay(void *p, uint32_t us) { ((struct fake *)p)->delay += us; }
static int powered(void *p) { ((struct fake *)p)->powered++; return !((struct fake *)p)->keep; }
int main(void)
{
    struct fake f = {.reg=0x222001};
    struct lilac_a540_cx_io io = {&f, read_reg, write_reg, delay, powered};
    struct lilac_a540_cx_result r;
    lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_OK && f.writes == 2 && f.powered == 1 && f.reg == 0x222001 &&
           (r.status_on & LILAC_A540_CX_POWER_ON) && !(r.status_after & LILAC_A540_CX_POWER_ON));
    f = (struct fake){.reg=0x222000}; lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_INHERITED_STATE && f.writes == 0);
    f = (struct fake){.reg=0x222001,.status=LILAC_A540_CX_POWER_ON}; lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_INHERITED_STATE && f.writes == 0);
    f = (struct fake){.reg=0x222001,.no_on=1}; lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_TIMEOUT && f.writes == 2 && f.powered == 0 && f.reg == 0x222001 && f.delay == 2001);
    f = (struct fake){.reg=0x222001,.no_vote=1}; lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_VOTE_FAILED && f.writes == 2 && f.reg == 0x222001);
    f = (struct fake){.reg=0x222001,.no_restore=1}; lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_RESTORE_FAILED && f.writes == 2);
    f = (struct fake){.reg=0x222001,.keep=1}; lilac_a540_cx_cycle(&io, &r);
    assert(r.result == LILAC_CX_RETAINED && f.writes == 1 && (f.status & LILAC_A540_CX_POWER_ON));
    puts("PASS: GPU CX vote, inherited state refusal, timeout and failed restore");
}
