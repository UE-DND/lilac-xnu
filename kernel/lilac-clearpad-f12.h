/* SPDX-License-Identifier: GPL-2.0-only
 * S3330 F12 eight-byte object records. This is a pure decoder: discovering
 * registers, acquiring a coherent frame and delivering events are separate.
 * Layout: Sony clearpad_core.c, clearpad_parse_finger_n_f12 (LA.UM.6.4.r1).
 */
#ifndef LILAC_CLEARPAD_F12_H
#define LILAC_CLEARPAD_F12_H
#include <stddef.h>
#include <stdint.h>

#define LILAC_F12_SLOTS 10u
#define LILAC_F12_BYTES 8u
enum lilac_touch_kind { LILAC_TOUCH_DOWN = 1, LILAC_TOUCH_MOVE, LILAC_TOUCH_UP };
struct lilac_f12_point { uint16_t x, y; uint8_t pressure, active; };
struct lilac_f12_state { struct lilac_f12_point slot[LILAC_F12_SLOTS]; };
struct lilac_f12_event { enum lilac_touch_kind kind; uint8_t slot; uint16_t x, y; uint8_t pressure; };

/* Register numbers are packed by presence; 0xff means absent. The caller
 * supplies the presence bytes after stripping the query's size field. */
static inline int
lilac_f12_offsets(const uint8_t *presence, size_t bytes, uint8_t *offsets, size_t count)
{
    uint8_t result[256]; unsigned offset = 0;
    if (!presence || !offsets || !bytes || bytes > 32 || count > sizeof(result) || count > bytes * 8) return 0;
    for (size_t i = 0; i < count; i++) {
        if (presence[i / 8] & (1u << (i % 8))) {
            if (offset >= 255) return 0;
            result[i] = (uint8_t)offset++;
        } else result[i] = 0xff;
    }
    for (size_t i = 0; i < count; i++) offsets[i] = result[i];
    return 1;
}

/* attention determines the number of records read, not whether a slot is a
 * finger. A malformed frame changes neither state nor caller output. */
static inline int
lilac_f12_frame(struct lilac_f12_state *state, uint16_t attention,
    const uint8_t *data, size_t length, struct lilac_f12_event *events,
    size_t capacity, size_t *event_count)
{
    struct lilac_f12_state next = {0};
    struct lilac_f12_event pending[LILAC_F12_SLOTS];
    size_t records = 0, count = 0;
    if (!state || !events || !event_count || (attention & ~0x3ffu)) return 0;
    for (uint16_t bits = attention; bits; bits >>= 1) records++;
    if (length != records * LILAC_F12_BYTES || (length && !data)) return 0;
    for (size_t i = 0; i < records; i++) {
        const uint8_t *p = data + i * LILAC_F12_BYTES;
        if (p[0] > 8) return 0;
        if (p[0] == 1) { /* default configuration reports fingers only */
            unsigned x = (unsigned)p[1] | (unsigned)p[2] << 8;
            unsigned y = (unsigned)p[3] | (unsigned)p[4] << 8;
            if (x > 719 || y > 1279) return 0;
            next.slot[i] = (struct lilac_f12_point){(uint16_t)x, (uint16_t)y, p[5], 1};
        }
    }
    for (size_t i = 0; i < LILAC_F12_SLOTS; i++) {
        const struct lilac_f12_point *old = &state->slot[i], *now = &next.slot[i];
        if (!old->active && !now->active) continue;
        if (old->active && now->active && old->x == now->x && old->y == now->y && old->pressure == now->pressure) continue;
        const struct lilac_f12_point *p = now->active ? now : old;
        pending[count++] = (struct lilac_f12_event){now->active ? (old->active ? LILAC_TOUCH_MOVE : LILAC_TOUCH_DOWN) : LILAC_TOUCH_UP,
            (uint8_t)i, p->x, p->y, p->pressure};
    }
    if (capacity < count) return 0;
    for (size_t i = 0; i < count; i++) events[i] = pending[i];
    *state = next;
    *event_count = count;
    return 1;
}
#endif
