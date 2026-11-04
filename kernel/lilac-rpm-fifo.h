/* SPDX-License-Identifier: GPL-2.0-only
 * RPM GLINK FIFO access. A caller must serialize operations, validate the
 * layout, negotiate channel ownership, and implement device barriers. These
 * helpers do not reset queue indices or make a regulator request.
 */
#ifndef LILAC_RPM_FIFO_H
#define LILAC_RPM_FIFO_H
#include "lilac-rpm-wire.h"

struct lilac_rpm_io {
    void *context;
    int (*read32)(void *, uint32_t, uint32_t *);
    int (*write32)(void *, uint32_t, uint32_t);
    void (*barrier)(void *);
    int (*notify)(void *);
};
struct lilac_rpm_queue {
    struct lilac_rpm_fifo fifo;
    int poisoned; /* a published write or notification may have reached RPM */
};

static inline int lilac_rpm_queue_valid(const struct lilac_rpm_queue *q, const struct lilac_rpm_io *io)
{
    return q && io && io->read32 && io->write32 && io->barrier && io->notify && !q->poisoned &&
        q->fifo.size >= 24 && !(q->fifo.size & 7) && !(q->fifo.descriptor & 3) &&
        q->fifo.data == q->fifo.descriptor + 8 && q->fifo.data >= q->fifo.descriptor &&
        (uint64_t)q->fifo.data + q->fifo.size <= 0x7000;
}
static inline int lilac_rpm_indices(const struct lilac_rpm_queue *q, const struct lilac_rpm_io *io,
                                     uint32_t *read, uint32_t *write)
{
    if (!lilac_rpm_queue_valid(q, io) || !read || !write ||
        !io->read32(io->context, q->fifo.descriptor, read) ||
        !io->read32(io->context, q->fifo.descriptor + 4, write)) return 0;
    io->barrier(io->context);
    return *read < q->fifo.size && *write < q->fifo.size && !((*read | *write) & 7);
}
/* Send exactly one 8-byte-aligned wire packet, reserving 16 bytes as Sony's
 * RPM transport does. Data becomes visible only after the write index. */
static inline int lilac_rpm_send(struct lilac_rpm_queue *q, const struct lilac_rpm_io *io,
                                 const uint8_t *packet, uint32_t length)
{
    uint32_t r, w;
    if (!packet || !length || (length & 7) || !lilac_rpm_indices(q, io, &r, &w)) return 0;
    uint32_t free = r > w ? r - w : q->fifo.size - w + r;
    if (free < 16 || length > free - 16) return 0;
    for (uint32_t i = 0; i < length; i += 4) {
        uint32_t at = (uint32_t)(((uint64_t)w + i) % q->fifo.size);
        if (!io->write32(io->context, q->fifo.data + at, lilac_rpm_get32(packet + i))) return 0;
    }
    io->barrier(io->context);
    uint32_t next = (uint32_t)(((uint64_t)w + length) % q->fifo.size);
    if (!io->write32(io->context, q->fifo.descriptor + 4, next)) { q->poisoned = 1; return 0; }
    io->barrier(io->context);
    if (!io->notify(io->context)) { q->poisoned = 1; return 0; }
    return 1;
}
/* Copy available bytes without consuming them. Length must be aligned; the
 * caller verifies the entire message before publishing a read-index change. */
static inline int lilac_rpm_peek(const struct lilac_rpm_queue *q, const struct lilac_rpm_io *io,
                                 uint8_t *packet, uint32_t length)
{
    uint32_t r, w;
    if (!packet || !length || (length & 7) || !lilac_rpm_indices(q, io, &r, &w)) return 0;
    uint32_t available = w >= r ? w - r : q->fifo.size - r + w;
    if (length > available) return 0;
    for (uint32_t i = 0; i < length; i += 4) {
        uint32_t value, at = (uint32_t)(((uint64_t)r + i) % q->fifo.size);
        if (!io->read32(io->context, q->fifo.data + at, &value)) return 0;
        lilac_rpm_put32(packet + i, value);
    }
    return 1;
}
static inline int lilac_rpm_consume(struct lilac_rpm_queue *q, const struct lilac_rpm_io *io,
                                    uint32_t length)
{
    uint32_t r, w;
    if (!length || (length & 7) || !lilac_rpm_indices(q, io, &r, &w)) return 0;
    uint32_t available = w >= r ? w - r : q->fifo.size - r + w;
    if (length > available) return 0;
    io->barrier(io->context);
    uint32_t next = (uint32_t)(((uint64_t)r + length) % q->fifo.size);
    if (!io->write32(io->context, q->fifo.descriptor, next)) { q->poisoned = 1; return 0; }
    io->barrier(io->context);
    if (!io->notify(io->context)) { q->poisoned = 1; return 0; }
    return 1;
}
#endif
