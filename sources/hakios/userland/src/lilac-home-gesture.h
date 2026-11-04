#ifndef LILAC_HOME_GESTURE_H
#define LILAC_HOME_GESTURE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "hakios.input.h"

/* The SO-02K has no Home key. Recognize a deliberate three-finger tap,
 * after every finger has lifted, without taking ordinary touches away from
 * the digitizer. A fourth finger cancels it, including the kernel's five-
 * finger return gesture. Times are sample times in microseconds. */
#define LILAC_HOME_JOIN_US  180000u
#define LILAC_HOME_TAP_US   600000u
#define LILAC_HOME_SLOP    2048u /* normalized coordinates: about 22 x 40 pixels */

struct lilac_home_gesture {
    uint32_t active, seen, x[10], y[10];
    uint64_t started, last;
    bool chord, released, cancelled;
};

static inline uint32_t lilac_home_distance(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

static inline bool
lilac_home_gesture_update(struct lilac_home_gesture *g, const struct hakios_touch *t, uint64_t now,
    unsigned touch_mode, bool waking)
{
    /* 0xfe consumes this contact as tap-to-wake. 0xff disables ordinary
     * digitizer input, but must not disable our substitute for a hardware
     * Home key: the startup/lock UI can require Home in exactly that state. */
    if (waking || (touch_mode & 0xff) == 0xfe) {
        memset(g, 0, sizeof(*g));
        return false;
    }
    if (t->type < HAKIOS_TOUCH_DOWN || t->type > HAKIOS_TOUCH_UP) return false;
    if (t->finger >= 10) {
        g->cancelled = true;
        return false;
    }
    uint32_t bit = 1u << t->finger;
    if (t->type == HAKIOS_TOUCH_DOWN) {
        if (!g->active) {
            memset(g, 0, sizeof(*g));
            g->started = g->last = now;
        }
        if ((g->seen & bit) || g->released) g->cancelled = true;
        g->active |= bit;
        g->seen |= bit;
        g->x[t->finger] = t->x;
        g->y[t->finger] = t->y;
        if (__builtin_popcount(g->seen) > 3 || now - g->started > LILAC_HOME_JOIN_US)
            g->cancelled = true;
        if (__builtin_popcount(g->active) == 3) g->chord = true;
    } else if (!(g->active & bit)) {
        g->cancelled = true;
        return false;
    }
    if (now < g->last || now - g->started > LILAC_HOME_TAP_US || t->x > 65535 || t->y > 65535 ||
        lilac_home_distance(t->x, g->x[t->finger]) > LILAC_HOME_SLOP ||
        lilac_home_distance(t->y, g->y[t->finger]) > LILAC_HOME_SLOP)
        g->cancelled = true;
    g->last = now;
    if (t->type == HAKIOS_TOUCH_UP) {
        g->released = true;
        g->active &= ~bit;
        if (!g->active) {
            bool tapped = g->chord && !g->cancelled && now - g->started >= 40000;
            memset(g, 0, sizeof(*g));
            return tapped;
        }
    }
    return false;
}
#endif
