/* Button state filter. Call once per 16 ms; a failed read breaks the streak. */
#ifndef LILAC_BUTTON_STATE_H
#define LILAC_BUTTON_STATE_H
#include <stdbool.h>

struct lilac_button_state {
    bool armed, reported_down, candidate;
    unsigned consecutive;
};

/* Returns -1 for release, 1 for press, 0 for no event. A key must first
 * be observed released twice, so a held key at startup is never invented. */
static inline int
lilac_button_sample(struct lilac_button_state *s, bool valid, bool down)
{
    if (!valid) {
        s->consecutive = 0;
        return 0;
    }
    if (!s->consecutive || s->candidate != down) {
        s->candidate = down;
        s->consecutive = 1;
        return 0;
    }
    if (s->consecutive < 2) s->consecutive++;
    if (!s->armed) {
        if (!down) s->armed = true;
        return 0;
    }
    if (s->reported_down == down) return 0;
    s->reported_down = down;
    return down ? 1 : -1;
}
#endif
