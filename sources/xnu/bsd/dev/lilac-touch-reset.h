#ifndef LILAC_TOUCH_RESET_H
#define LILAC_TOUCH_RESET_H
#include <stdint.h>

struct lilac_touch_reset_io {
    void *context;
    uint32_t (*read)(void *, unsigned);
    void (*write)(void *, unsigned, uint32_t);
    void (*delay_us)(void *, unsigned);
};

enum { LILAC_TOUCH_RESET_CONFIG, LILAC_TOUCH_RESET_IO,
       LILAC_TOUCH_VDDIO_CONFIG, LILAC_TOUCH_VDDIO_IO };

static inline int lilac_touch_release_reset(const struct lilac_touch_reset_io *io)
{
    uint32_t reset = io->read(io->context, LILAC_TOUCH_RESET_CONFIG);
    uint32_t power = io->read(io->context, LILAC_TOUCH_VDDIO_CONFIG);
    if ((reset & 0x23c) != 0x200 || (power & 0x23c) != 0x200 ||
        (io->read(io->context, LILAC_TOUCH_VDDIO_IO) & 3)) return 110;
    uint32_t value = io->read(io->context, LILAC_TOUCH_RESET_IO);
    if (!(value & 2)) {
        io->write(io->context, LILAC_TOUCH_RESET_IO, value | 2);
        io->delay_us(io->context, 100000);
    }
    return (io->read(io->context, LILAC_TOUCH_RESET_IO) & 3) == 3 ? 0 : 111;
}
#endif
