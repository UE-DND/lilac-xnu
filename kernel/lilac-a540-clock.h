/* SPDX-License-Identifier: GPL-2.0-only
 * MSM8998 v2 Fabia PLL, Sony 696bf916 clock-alpha-pll.c. The caller drains
 * CP before switching. 180/257 MHz share the validated corner ceiling
 * (724 mV, MX 0x80); higher OPPs need CPR/thermal policy and are rejected.
 */
#ifndef LILAC_A540_CLOCK_H
#define LILAC_A540_CLOCK_H
#include "lilac-a540-power.h"
struct lilac_a540_clock { uint32_t saved[7]; int owned; uint32_t hz; };
static const uint32_t lilac_pll_offsets[7] = {0,4,8,12,0x2c,0x38,0x10};
static inline int lilac_a540_clock_set(const struct lilac_a540_power_io *io,
    struct lilac_a540_clock *c, uint32_t hz)
{
    if (hz != 19200000 && hz != 180000000 && hz != 257000000) return 0;
    struct lilac_a540_power p = {0};
    if (!c->owned) {
        for (unsigned i=0;i<7;i++) if (!io->read(io->context,lilac_pll_offsets[i],&c->saved[i])) return 0;
        /* Never reprogram a PLL already driven by another owner. */
        if ((c->saved[0] & 1) || c->saved[4]) return 0;
        c->owned=1;
    }
    /* Transfer the live RCG to XO before disabling or retuning the PLL. */
    if (!io->write(io->context,0x1074,0) || !lilac_a540_power_trigger(io,&p,0x1070,3) ||
        !lilac_a540_power_poll(io,&p,0x1070,0x80000001,0) ||
        !lilac_a540_power_update(io,&p,0,1,0) ||
        !lilac_a540_power_update(io,&p,12,7,0) || !io->write(io->context,0x2c,0)) return 0;
    c->hz=19200000;
    if (hz==19200000) return 1;
    uint64_t scaled=((uint64_t)hz*2*65536+19199999)/19200000;
    if (!io->write(io->context,4,(uint32_t)(scaled>>16)) ||
        !io->write(io->context,8,(uint32_t)(scaled>>16)) ||
        !io->write(io->context,0x38,(uint32_t)scaled&65535) ||
        !lilac_a540_power_update(io,&p,12,0xf00,0x100) ||
        !lilac_a540_power_update(io,&p,0,4,4) || !io->write(io->context,0x2c,1) ||
        !lilac_a540_power_poll(io,&p,0,0x80000000,0x80000000) ||
        !lilac_a540_power_update(io,&p,12,7,7) || !lilac_a540_power_update(io,&p,0,1,1) ||
        !io->write(io->context,0x1074,0x101) || !lilac_a540_power_trigger(io,&p,0x1070,1) ||
        !lilac_a540_power_poll(io,&p,0x1070,0x80000001,0)) return 0;
    uint32_t l,frac,cfg,mode;
    if (!io->read(io->context,4,&l) || !io->read(io->context,0x38,&frac) ||
        !io->read(io->context,0x1074,&cfg) || !io->read(io->context,0,&mode) ||
        l!=(scaled>>16) || frac!=(scaled&65535) || cfg!=0x101 || (mode&0x80000001)!=0x80000001) return 0;
    c->hz=hz;
    return 1;
}
static inline int lilac_a540_clock_restore(const struct lilac_a540_power_io *io,
    struct lilac_a540_clock *c)
{
    if (!c->owned) return 1;
    if (!lilac_a540_clock_set(io,c,19200000)) return 0;
    /* Output is off. Restore configuration before its original reset state. */
    for (unsigned i=1;i<7;i++) if (!io->write(io->context,lilac_pll_offsets[i],c->saved[i])) return 0;
    if (!io->write(io->context,0,c->saved[0]&0x7fffffff)) return 0;
    c->owned=0;
    return 1;
}
#endif
