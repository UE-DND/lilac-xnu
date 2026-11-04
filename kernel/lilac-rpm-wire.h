/* SPDX-License-Identifier: GPL-2.0-only
 * MSM8998 RPM message-RAM layout and rwmx level request encoding.
 * This file performs no MMIO and does not establish a GLINK channel.
 * A caller must establish exclusive ownership and negotiate the protocol
 * before publishing any packet or changing shared FIFO indices.
 */
#ifndef LILAC_RPM_WIRE_H
#define LILAC_RPM_WIRE_H
#include <stddef.h>
#include <stdint.h>

#define LILAC_RPM_TOC_SIZE 256u
#define LILAC_RPM_TOC_ID 0x67727430u
#define LILAC_RPM_TX_ID 0x61703272u
#define LILAC_RPM_RX_ID 0x72326170u
#define LILAC_RPM_REV1 0x31726576u
struct lilac_rpm_fifo { uint32_t descriptor, data, size; };
struct lilac_rpm_layout { struct lilac_rpm_fifo tx, rx; };

static inline uint32_t lilac_rpm_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void lilac_rpm_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
/* Parse a stable snapshot, never the concurrently changing device memory. */
static inline int lilac_rpm_layout_toc(const uint8_t *table, size_t size, struct lilac_rpm_layout *result)
{
    if (!table || !result || size < LILAC_RPM_TOC_SIZE || size > UINT32_MAX) return 0;
    size_t toc = size - LILAC_RPM_TOC_SIZE;
    if (lilac_rpm_get32(table) != LILAC_RPM_TOC_ID) return 0;
    uint32_t count = lilac_rpm_get32(table + 4), found = 0;
    if (count > 20) return 0;
    struct lilac_rpm_layout out = {0};
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = table + 8 + 12 * i;
        uint32_t id = lilac_rpm_get32(entry), offset = lilac_rpm_get32(entry + 4), n = lilac_rpm_get32(entry + 8);
        if (id != LILAC_RPM_TX_ID && id != LILAC_RPM_RX_ID) continue;
        uint32_t bit = id == LILAC_RPM_TX_ID ? 1u : 2u;
        if ((found & bit) || (offset & 7u) || (n & 7u) || n < 16 ||
            offset > toc || toc - offset < 8 || n > toc - offset - 8) return 0;
        struct lilac_rpm_fifo f = {offset, offset + 8, n};
        if (bit == 1) out.tx = f; else out.rx = f;
        found |= bit;
    }
    if (found != 3) return 0;
    uint64_t tx_end = (uint64_t)out.tx.data + out.tx.size;
    uint64_t rx_end = (uint64_t)out.rx.data + out.rx.size;
    if (out.tx.descriptor < rx_end && out.rx.descriptor < tx_end) return 0;
    *result = out;
    return 1;
}
static inline int lilac_rpm_layout(const uint8_t *ram, size_t size, struct lilac_rpm_layout *result)
{
    if (!ram || size < LILAC_RPM_TOC_SIZE || size > UINT32_MAX) return 0;
    return lilac_rpm_layout_toc(ram + size - LILAC_RPM_TOC_SIZE, size, result);
}
/* version is selected by the RPM version register, not guessed. set is the
 * Sony msm_rpm_set value. No voltage level is chosen by this helper. */
static inline size_t lilac_rpm_rwmx_request(uint8_t *out, size_t capacity, unsigned version,
                                             uint32_t msg_id, uint32_t set, uint32_t level)
{
    size_t bytes = version == 0 ? 40 : version == 1 ? 28 : 0;
    if (!out || !bytes || capacity < bytes || msg_id < 2 || set > 15 || level > 0xffff) return 0;
    if (version == 0) {
        lilac_rpm_put32(out, 0x00716572u); /* req */
        lilac_rpm_put32(out + 4, 32);
        lilac_rpm_put32(out + 8, msg_id);
        lilac_rpm_put32(out + 12, set);
        lilac_rpm_put32(out + 16, 0x786d7772u); /* rwmx */
        lilac_rpm_put32(out + 20, 0);
        lilac_rpm_put32(out + 24, 12);
        out += 28;
    } else {
        lilac_rpm_put32(out, 0x01000018u);
        lilac_rpm_put32(out + 4, msg_id);
        lilac_rpm_put32(out + 8, 0x786d7772u);
        lilac_rpm_put32(out + 12, (set << 28) | 12);
        out += 16;
    }
    lilac_rpm_put32(out, 0x6c766c76u); /* vlvl */
    lilac_rpm_put32(out + 4, 4);
    lilac_rpm_put32(out + 8, level);
    return bytes;
}
#endif
