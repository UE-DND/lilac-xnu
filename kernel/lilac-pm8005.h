/* SPDX-License-Identifier: GPL-2.0-only
 * PM8005 S1 (PPID 0x514), MSM8998 PMIC arbiter v2/v3. Register formats:
 * Sony 696bf916 drivers/spmi/spmi-pmic-arb.c and qpnp-regulator.c.
 * No other peripheral or rail can be written through this interface.
 */
#ifndef LILAC_PM8005_H
#define LILAC_PM8005_H
#include <stdint.h>

struct lilac_pm8005_io {
    void *context;
    int (*read)(void *, uint64_t, uint32_t *);
    int (*write)(void *, uint64_t, uint32_t);
    void (*delay)(void *, uint32_t);
};
struct lilac_pm8005 {
    uint32_t channel, version, last_status;
    uint16_t millivolts;
    uint8_t type, subtype, mode, enable, status, available, touched;
};

static inline int lilac_pm8005_channel(const struct lilac_pm8005_io *io, struct lilac_pm8005 *p)
{
    uint32_t value;
    if (!io || !p || !io->read || !io->write || !io->delay) return 0;
    *p = (struct lilac_pm8005){0};
    if (!io->read(io->context, 0x0800f000, &p->version) ||
        p->version < 0x20010000 || p->version >= 0x50000000) return 0;
    for (uint32_t channel = 0; channel < 511; channel++) {
        if (!io->read(io->context, 0x0800f800 + 4 * channel, &value)) return 0;
        if (((value >> 8) & 0xfff) != 0x514) continue;
        if (!io->read(io->context, 0x0800a700 + 4 * channel, &value) || (value & 7)) return 0;
        p->channel = channel;
        p->available = 1;
        return 1;
    }
    return 0;
}
static inline int lilac_pm8005_transfer(const struct lilac_pm8005_io *io, struct lilac_pm8005 *p,
    uint32_t offset, uint32_t bytes, uint32_t *value, int write)
{
    if (!p->available || !value ||
        !((bytes == 2 && offset == 0x40) || (bytes == 1 &&
          (offset == 4 || offset == 5 || offset == 8 || offset == 0x45 || offset == 0x46))) ||
        (write && (offset < 0x40 || p->type != 0x1c || p->subtype != 0x0a))) return 0;
    uint64_t base = (write ? 0x08400000u : 0x09400000u) + 0x8000u * p->channel;
    if (write) {
        p->touched = 1; /* even a reported failure may have reached the bus */
        if (!io->write(io->context, base + 0x10, *value)) return 0;
    }
    if (!io->write(io->context, base, (write ? 0 : 1u << 27) | (offset << 4) | (bytes - 1))) return 0;
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        if (!io->read(io->context, base + 8, &p->last_status)) return 0;
        if (p->last_status & 1) {
            if (p->last_status & 14) return 0;
            if (!write && !io->read(io->context, base + 0x18, value)) return 0;
            return 1;
        }
        io->delay(io->context, 1);
    }
    return 0;
}
static inline int lilac_pm8005_snapshot(const struct lilac_pm8005_io *io, struct lilac_pm8005 *p)
{
    uint32_t value;
    if (!lilac_pm8005_channel(io, p)) return 0;
    if (!lilac_pm8005_transfer(io,p,4,1,&value,0)) return 0;
    p->type = value;
    if (!lilac_pm8005_transfer(io,p,5,1,&value,0)) return 0;
    p->subtype = value;
    if (p->type != 0x1c || p->subtype != 0x0a) return 0;
    if (!lilac_pm8005_transfer(io,p,0x40,2,&value,0)) return 0;
    p->millivolts = value;
    if (!lilac_pm8005_transfer(io,p,0x45,1,&value,0)) return 0;
    p->mode = value;
    if (!lilac_pm8005_transfer(io,p,0x46,1,&value,0)) return 0;
    p->enable = value;
    if (!lilac_pm8005_transfer(io,p,8,1,&value,0)) return 0;
    p->status = value;
    return 1;
}
static inline int lilac_pm8005_set(const struct lilac_pm8005_io *io, struct lilac_pm8005 *p,
    uint32_t offset, uint32_t bytes, uint32_t value)
{
    uint32_t readback;
    return lilac_pm8005_transfer(io,p,offset,bytes,&value,1) &&
        lilac_pm8005_transfer(io,p,offset,bytes,&readback,0) &&
        (readback & (bytes == 1 ? 0xffu : 0xffffu)) == value;
}
/* Fixed low-frequency open-loop operation uses the DT corner-1 ceiling;
 * fuse revision 0: 716 mV, others: 724 mV. No DVFS or CPR feedback here. */
static inline int lilac_pm8005_on(const struct lilac_pm8005_io *io, struct lilac_pm8005 *p, uint32_t mv)
{
    if (!p->available || (p->enable & 0x80) || (mv != 716 && mv != 724)) return 0;
    if (!lilac_pm8005_set(io,p,0x40,2,mv) || !lilac_pm8005_set(io,p,0x45,1,7) ||
        !lilac_pm8005_set(io,p,0x46,1,p->enable | 0x80)) return 0;
    io->delay(io->context, 500);
    for (unsigned i = 0; i < 500; i++) {
        uint32_t status;
        if (!lilac_pm8005_transfer(io,p,8,1,&status,0)) return 0;
        if (status & 0x80) return 1;
        io->delay(io->context, 10);
    }
    return 0;
}
/* The caller must collapse GX and clamp its IO before calling this. */
static inline int lilac_pm8005_restore(const struct lilac_pm8005_io *io, struct lilac_pm8005 *p)
{
    if (!p->touched) return 1;
    if (!lilac_pm8005_set(io,p,0x46,1,p->enable)) return 0;
    io->delay(io->context, 500);
    int mode = lilac_pm8005_set(io,p,0x45,1,p->mode);
    int voltage = lilac_pm8005_set(io,p,0x40,2,p->millivolts);
    if (mode && voltage) p->touched = 0;
    return mode && voltage;
}
#endif
