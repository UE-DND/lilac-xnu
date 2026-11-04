#ifndef LILAC_DEBUG_REBOOT_H
#define LILAC_DEBUG_REBOOT_H
#include <stdint.h>

#define LILAC_RESTART_REASON 0x146bf65cu
#define LILAC_PS_HOLD 0x010ac000u

struct lilac_reboot_io {
    int (*read)(uint64_t address, uint32_t *value);
    int (*write)(uint64_t address, uint32_t value);
    void (*delay)(unsigned microseconds);
};

static int lilac_pon_channel(const struct lilac_reboot_io *io, unsigned *channel)
{
    uint32_t value;
    if (!io->read(0x0800f000, &value) || value < 0x20010000 || value >= 0x50000000) return 0;
    for (unsigned candidate = 0; candidate < 511; candidate++) {
        if (!io->read(0x0800f800 + 4 * candidate, &value)) return 0;
        if (value && ((value >> 8) & 0xfff) == 8) {
            if (!io->read(0x0800a700 + 4 * candidate, &value) || (value & 7)) return 0;
            *channel = candidate;
            return 1;
        }
    }
    return 0;
}

static int lilac_pon_byte(const struct lilac_reboot_io *io, unsigned channel,
    unsigned offset, uint8_t *value, int write)
{
    if (channel >= 511 || (offset != 5 && offset != 0x5a && offset != 0x5b && offset != 0x8f) ||
        (write && offset == 5)) return 0;
    uint64_t base = (write ? 0x08400000u : 0x09400000u) + 0x8000u * channel;
    if (write && !io->write(base + 0x10, *value)) return 0;
    if (!io->write(base, (write ? 0 : 1u << 27) | (offset << 4))) return 0;
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        uint32_t status;
        if (!io->read(base + 8, &status)) return 0;
        if (status & 1) {
            if (status & 14) return 0;
            if (!write) {
                if (!io->read(base + 0x18, &status)) return 0;
                *value = status;
            }
            return 1;
        }
        io->delay(1);
    }
    return 0;
}

static int lilac_pon_write(const struct lilac_reboot_io *io, unsigned channel,
    unsigned offset, uint8_t value)
{
    uint8_t actual;
    return lilac_pon_byte(io, channel, offset, &value, 1) &&
        lilac_pon_byte(io, channel, offset, &actual, 0) && actual == value;
}

static int lilac_prepare_fastboot(const struct lilac_reboot_io *io)
{
    unsigned channel;
    uint32_t old_reason, value;
    uint8_t subtype, control, enable, reason;
    if (!lilac_pon_channel(io, &channel) ||
        !lilac_pon_byte(io, channel, 5, &subtype, 0) || subtype != 4 ||
        !lilac_pon_byte(io, channel, 0x5a, &control, 0) ||
        !lilac_pon_byte(io, channel, 0x5b, &enable, 0) ||
        !lilac_pon_byte(io, channel, 0x8f, &reason, 0) ||
        !io->read(LILAC_RESTART_REASON, &old_reason) ||
        !io->read(LILAC_PS_HOLD, &value) || value != 1) return 0;
    if (!lilac_pon_write(io, channel, 0x5b, enable & ~0x80u)) goto restore;
    io->delay(500);
    if (!lilac_pon_write(io, channel, 0x5a, (control & ~15u) | 1) ||
        !lilac_pon_write(io, channel, 0x5b, enable | 0x80) ||
        !lilac_pon_write(io, channel, 0x8f, (reason & 1) | (3u << 1)) ||
        !io->write(LILAC_RESTART_REASON, 0x77665500) ||
        !io->read(LILAC_RESTART_REASON, &value) || value != 0x77665500) goto restore;
    return 1;
restore:
    lilac_pon_write(io, channel, 0x5b, enable & ~0x80u);
    io->delay(500);
    lilac_pon_write(io, channel, 0x5a, control);
    lilac_pon_write(io, channel, 0x5b, enable);
    lilac_pon_write(io, channel, 0x8f, reason);
    io->write(LILAC_RESTART_REASON, old_reason);
    return 0;
}
#endif
