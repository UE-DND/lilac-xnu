/* SPDX-License-Identifier: BSD-2-Clause
 * MSM8998 GPU CX GDSC vote, for a bounded bring-up experiment. The GX
 * regulator, GPU clocks and SMMU are not owned by this operation.
 */
#ifndef LILAC_A540_CX_H
#define LILAC_A540_CX_H
#include <stdint.h>

#define LILAC_A540_CX_GDSCR 0x1004u
#define LILAC_A540_CX_HW_STATUS 0x1008u
#define LILAC_A540_CX_COLLAPSE 1u
#define LILAC_A540_CX_HW_CONTROL 2u
#define LILAC_A540_CX_POWER_ON (1u << 31)

struct lilac_a540_cx_io {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    void (*delay_us)(void *, uint32_t);
    int (*on_powered)(void *); /* zero retains CX: a child cannot safely collapse */
};

enum lilac_a540_cx_error {
    LILAC_CX_OK = 0, LILAC_CX_BAD_ARGUMENT, LILAC_CX_INHERITED_STATE,
    LILAC_CX_VOTE_FAILED, LILAC_CX_TIMEOUT, LILAC_CX_RESTORE_FAILED, LILAC_CX_RETAINED
};

struct lilac_a540_cx_result {
    uint32_t result, before, status_before, voted, status_on, restored, status_after;
};

/* Exercise only a previously collapsed and unvoted CX. Restore our vote on
 * every path after a write; never switch off a domain inherited as active. */
static inline void lilac_a540_cx_cycle(const struct lilac_a540_cx_io *io,
                                       struct lilac_a540_cx_result *r)
{
    if (!r) return;
    *r = (struct lilac_a540_cx_result){0};
    if (!io || !io->read || !io->write || !io->delay_us) {
        r->result = LILAC_CX_BAD_ARGUMENT; return;
    }
    r->before = io->read(io->context, LILAC_A540_CX_GDSCR);
    r->status_before = io->read(io->context, LILAC_A540_CX_HW_STATUS);
    if (!(r->before & LILAC_A540_CX_COLLAPSE) ||
        (r->before & LILAC_A540_CX_HW_CONTROL) ||
        (r->status_before & LILAC_A540_CX_POWER_ON)) {
        r->result = LILAC_CX_INHERITED_STATE; return;
    }
    io->write(io->context, LILAC_A540_CX_GDSCR, r->before & ~LILAC_A540_CX_COLLAPSE);
    r->voted = io->read(io->context, LILAC_A540_CX_GDSCR);
    if (r->voted & LILAC_A540_CX_COLLAPSE) r->result = LILAC_CX_VOTE_FAILED;
    else {
        io->delay_us(io->context, 1);
        for (unsigned i = 0; i < 1500; i++) {
            r->status_on = io->read(io->context, LILAC_A540_CX_HW_STATUS);
            if (r->status_on & LILAC_A540_CX_POWER_ON) break;
            io->delay_us(io->context, 1);
        }
        if (!(r->status_on & LILAC_A540_CX_POWER_ON)) r->result = LILAC_CX_TIMEOUT;
        else {
            /* Let the domain's memory and clocks settle after power-on. */
            io->delay_us(io->context, 1);
            if (io->on_powered && !io->on_powered(io->context)) {
                r->result = LILAC_CX_RETAINED; return;
            }
        }
    }
    uint32_t current = io->read(io->context, LILAC_A540_CX_GDSCR);
    io->write(io->context, LILAC_A540_CX_GDSCR, current | LILAC_A540_CX_COLLAPSE);
    r->restored = io->read(io->context, LILAC_A540_CX_GDSCR);
    io->delay_us(io->context, 500); /* CX is votable; another master may keep it on. */
    r->status_after = io->read(io->context, LILAC_A540_CX_HW_STATUS);
    if (!(r->restored & LILAC_A540_CX_COLLAPSE)) r->result = LILAC_CX_RESTORE_FAILED;
}
#endif
