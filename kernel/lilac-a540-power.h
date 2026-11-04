/* SPDX-License-Identifier: GPL-2.0-only
 * A540 GX sequencing from Sony clock-gpu-8998.c / Linux v6.12 gdsc.c.
 * Caller owns CX, has an acknowledged MX vote and a stable GX regulator.
 * Core uses XO (19.2 MHz) during initial command-processor bring-up.
 */
#ifndef LILAC_A540_POWER_H
#define LILAC_A540_POWER_H
#include <stdint.h>
struct lilac_a540_power_io {
    void *context;
    int (*read)(void *, uint32_t, uint32_t *);
    int (*write)(void *, uint32_t, uint32_t);
    void (*delay)(void *, uint32_t);
};
struct lilac_a540_power {
    uint32_t root_config, root_command, timer_config, timer_command, clamp;
    uint32_t last_offset, last_value;
    int touched, gx_touched, ready;
};
static inline int lilac_a540_power_trigger(const struct lilac_a540_power_io *io,
    struct lilac_a540_power *p, uint32_t offset, uint32_t bits)
{
    uint32_t before;
    p->last_offset = offset;
    return io->read(io->context,offset,&before) && io->write(io->context,offset,before | bits);
}
static inline int lilac_a540_power_update(const struct lilac_a540_power_io *io,
    struct lilac_a540_power *p, uint32_t offset, uint32_t mask, uint32_t value)
{
    uint32_t before;
    p->last_offset = offset;
    return io->read(io->context,offset,&before) &&
        io->write(io->context,offset,(before & ~mask) | value) &&
        io->read(io->context,offset,&p->last_value) && (p->last_value & mask) == value;
}
static inline int lilac_a540_power_poll(const struct lilac_a540_power_io *io,
    struct lilac_a540_power *p, uint32_t offset, uint32_t mask, uint32_t value)
{
    p->last_offset = offset;
    for (unsigned i = 0; i < 1500; i++) {
        if (!io->read(io->context,offset,&p->last_value)) return 0;
        if ((p->last_value & mask) == value) { io->delay(io->context,1); return 1; }
        io->delay(io->context,1);
    }
    return 0;
}
/* Must call release after any failed acquire too. */
static inline int lilac_a540_power_acquire(const struct lilac_a540_power_io *io, struct lilac_a540_power *p)
{
    uint32_t gx, core, timer, reset, xo;
    *p = (struct lilac_a540_power){0};
    if (!io->read(io->context,0x1094,&gx) || !io->read(io->context,0x1098,&core) ||
        !io->read(io->context,0x10d0,&timer) || !io->read(io->context,0x1090,&reset) ||
        !io->read(io->context,0x1020,&xo) || !io->read(io->context,0x130,&p->clamp) ||
        !io->read(io->context,0x1074,&p->root_config) || !io->read(io->context,0x1070,&p->root_command) ||
        !io->read(io->context,0x10b4,&p->timer_config) || !io->read(io->context,0x10b0,&p->timer_command)) return 0;
    if ((gx & 0x80000003u) != 1 || (core & 1) || (timer & 1) || reset ||
        (xo & 0x80000001u) != 1 || (p->clamp & 17) != 1 || (p->root_command & 3) ||
        (p->timer_command & 3)) return 0;
    p->touched = 1;
    if (!io->write(io->context,0x1074,0) ||
        !lilac_a540_power_trigger(io,p,0x1070,3) ||
        !lilac_a540_power_poll(io,p,0x1070,0x80000001,0)) return 0;
    if (!io->write(io->context,0x1090,1)) return 0;
    io->delay(io->context,1);
    if (!io->write(io->context,0x1090,0) || !lilac_a540_power_update(io,p,0x130,16,16)) return 0;
    io->delay(io->context,1);
    if (!lilac_a540_power_update(io,p,0x130,17,0)) return 0;
    p->gx_touched = 1;
    if (!lilac_a540_power_update(io,p,0x1094,1,0) ||
        !lilac_a540_power_poll(io,p,0x1094,0x80000000,0x80000000) ||
        !lilac_a540_power_update(io,p,0x1098,1,1) ||
        !lilac_a540_power_poll(io,p,0x1098,0x80000000,0) ||
        !io->write(io->context,0x10b4,0) ||
        !lilac_a540_power_trigger(io,p,0x10b0,1) ||
        !lilac_a540_power_poll(io,p,0x10b0,1,0) ||
        !lilac_a540_power_update(io,p,0x10d0,1,1) ||
        !lilac_a540_power_poll(io,p,0x10d0,0x80000000,0)) return 0;
    p->ready = 1;
    return 1;
}
/* False means retain the upstream rails and CX until reboot. */
static inline int lilac_a540_power_release(const struct lilac_a540_power_io *io, struct lilac_a540_power *p)
{
    if (!p->touched) return 1;
    int ok = 1;
    if (p->gx_touched) {
        /* Keep the forced XO root running until GX is collapsed. */
        if (!lilac_a540_power_update(io,p,0x10d0,1,0)) ok = 0;
        if (!lilac_a540_power_update(io,p,0x1098,1,0)) ok = 0;
        if (!lilac_a540_power_update(io,p,0x1094,1,1) ||
            !lilac_a540_power_poll(io,p,0x1094,0x80000000,0)) return 0;
    }
    if (!lilac_a540_power_update(io,p,0x130,17,p->clamp & 17) ||
        !lilac_a540_power_update(io,p,0x1090,1,0)) return 0;
    if (!io->write(io->context,0x10b4,p->timer_config) ||
        !lilac_a540_power_trigger(io,p,0x10b0,1) || !lilac_a540_power_poll(io,p,0x10b0,1,0) ||
        !io->write(io->context,0x1074,p->root_config) ||
        !lilac_a540_power_trigger(io,p,0x1070,1) || !lilac_a540_power_poll(io,p,0x1070,1,0) ||
        !lilac_a540_power_update(io,p,0x1070,2,p->root_command & 2)) return 0;
    if (ok) p->touched = p->gx_touched = p->ready = 0;
    return ok;
}
#endif
