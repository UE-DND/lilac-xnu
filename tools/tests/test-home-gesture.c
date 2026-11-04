#include <assert.h>
#include <stdio.h>
#include "../../sources/hakios/userland/src/lilac-home-gesture.h"

static struct lilac_home_gesture gesture;
static unsigned homes;
static unsigned touch_mode;
static bool waking;

static void sample(unsigned type, unsigned slot, unsigned x, unsigned y, uint64_t time)
{
    struct hakios_touch t = {.type = type, .finger = slot, .x = x, .y = y};
    homes += lilac_home_gesture_update(&gesture, &t, time, touch_mode, waking);
}

static void down(unsigned slot, uint64_t time)
{
    sample(HAKIOS_TOUCH_DOWN, slot, 10000 + slot * 5000, 30000, time);
}

static void up(unsigned slot, uint64_t time)
{
    sample(HAKIOS_TOUCH_UP, slot, 10000 + slot * 5000, 30000, time);
}

static void reset(void)
{
    memset(&gesture, 0, sizeof(gesture));
    homes = 0;
    touch_mode = 0;
    waking = false;
}

int main(void)
{
    /* Arbitrary hardware slots and release order; emit once, on the last up. */
    down(0, 1000000); down(4, 1040000); down(9, 1080000);
    sample(HAKIOS_TOUCH_MOVE, 4, 30100, 30200, 1100000);
    up(4, 1120000); up(0, 1140000);
    assert(homes == 0);
    up(9, 1160000); assert(homes == 1);
    up(9, 1170000); assert(homes == 1);
    down(0, 2000000); down(1, 2010000); down(2, 2020000);
    up(0, 2100000); up(1, 2110000); up(2, 2120000);
    assert(homes == 2); /* no stale state or repeated trigger */

    for (unsigned count = 1; count <= 10; count++) {
        reset();
        for (unsigned i = 0; i < count; i++) down(i, 1000000 + i * 1000);
        for (unsigned i = 0; i < count; i++) up(i, 1200000 + i * 1000);
        assert(homes == (count == 3));
    }
    /* Five-finger hold remains available for returning to BootShim. */
    reset();
    for (unsigned i = 0; i < 5; i++) down(i, 1000000 + i * 1000);
    for (unsigned i = 0; i < 5; i++) up(i, 2600000 + i * 1000);
    assert(!homes);

    /* Reject movement even if the finger subsequently returns to its origin. */
    reset(); down(0, 1000000); down(1, 1010000); down(2, 1020000);
    sample(HAKIOS_TOUCH_MOVE, 1, 20000, 30000, 1050000);
    up(0, 1100000); up(1, 1110000); up(2, 1120000); assert(!homes);

    /* Slow joins, long holds, glitches, out-of-order times, repeated contacts,
     * invalid slots/positions, and interrupted contact streams cannot press Home. */
    for (unsigned fault = 0; fault < 10; fault++) {
        reset();
        down(0, 1000000); down(1, 1010000);
        down(2, fault == 0 ? 1300000 : 1020000);
        if (fault == 2) sample(HAKIOS_TOUCH_MOVE, 0, 10000, 30000, 900000);
        if (fault == 3) down(0, 1030000);
        if (fault == 4) down(32, 1030000);
        if (fault == 5) sample(HAKIOS_TOUCH_MOVE, 0, 65536, 30000, 1030000);
        if (fault == 6) { up(0, 1030000); down(0, 1040000); }
        if (fault == 7) { up(0, 1030000); down(3, 1040000); up(3, 1050000); }
        if (fault == 9) up(8, 1030000);
        uint64_t end = fault == 1 ? 1700000 : fault == 8 ? 1021000 : 1400000;
        up(0, end); up(1, end + 1000); up(2, end + 2000);
        assert(!homes);
    }
    reset();
    for (unsigned i = 0; i < 3; i++) { down(i, 1000000 + i * 100000); up(i, 1050000 + i * 100000); }
    assert(!homes); /* three sequential taps are not a chord */
    /* The lock/startup screen may disable its digitizer while requiring Home.
     * A software substitute for the hardware key must still work there. */
    const unsigned modes[] = {0, 1, 0xff, 0x1ff, 0xfe, 0x1fe};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        reset(); touch_mode = modes[i];
        down(0, 1000000); down(1, 1010000); down(2, 1020000);
        up(0, 1100000); up(1, 1110000); up(2, 1120000);
        assert(homes == ((touch_mode & 0xff) != 0xfe));
    }
    /* Switching to the awake mode does not reuse wake-up contacts as Home. */
    reset(); touch_mode = 0xfe;
    down(0, 1000000);
    touch_mode = 1; waking = true;
    down(1, 1010000); down(2, 1020000);
    up(0, 1100000); up(1, 1110000); up(2, 1120000);
    assert(!homes);
    waking = false;
    down(0, 2000000); down(1, 2010000); down(2, 2020000);
    up(0, 2100000); up(1, 2110000); up(2, 2120000);
    assert(homes == 1);
    puts("PASS: three-finger Home, release ordering, normal multitouch, five-finger return, movement and malformed streams");
    return 0;
}
