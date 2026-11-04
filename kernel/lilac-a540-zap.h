/* SPDX-License-Identifier: GPL-2.0-only
 * Bounds for the local A540 ELF32 split firmware. Parsing is not signature
 * authentication: only a successful Qualcomm PAS AUTH_AND_RESET is that.
 */
#ifndef LILAC_A540_ZAP_H
#define LILAC_A540_ZAP_H
#include <stdint.h>
#include <stddef.h>
struct lilac_a540_zap_layout { uint32_t file_bytes, memory_bytes, alignment; };
static inline uint32_t lilac_zap_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline int lilac_a540_zap_layout(const uint8_t *mdt, size_t bytes, size_t code_bytes,
    struct lilac_a540_zap_layout *out)
{
    if (!mdt || !out || bytes < 148 || bytes > 16384 ||
        lilac_zap_u32(mdt) != 0x464c457f || lilac_zap_u32(mdt + 4) != 0x00010101 ||
        lilac_zap_u32(mdt + 16) != 0x00a40002 || lilac_zap_u32(mdt + 20) != 1 ||
        lilac_zap_u32(mdt + 28) != 52 || mdt[40] != 52 || mdt[41] ||
        mdt[42] != 32 || mdt[43] || mdt[44] != 3 || mdt[45]) return 0;
    const uint8_t *hash = mdt + 84, *code = mdt + 116;
    uint32_t hashes = lilac_zap_u32(hash + 16), file = lilac_zap_u32(code + 16);
    uint32_t memory = lilac_zap_u32(code + 20), alignment = lilac_zap_u32(code + 28);
    if (lilac_zap_u32(mdt + 52) || lilac_zap_u32(mdt + 68) != 148 ||
        lilac_zap_u32(hash) || (lilac_zap_u32(hash + 24) & 0x7000000) != 0x2000000 ||
        hashes != bytes - 148 || lilac_zap_u32(code) != 1 ||
        lilac_zap_u32(code + 24) != 0x08000007 || (lilac_zap_u32(code + 12) & 4095) ||
        !file || file != code_bytes || file > memory || memory > 16384 ||
        alignment != 0x100000) return 0;
    *out = (struct lilac_a540_zap_layout){file, (memory + 4095) & ~4095u, alignment};
    return 1;
}
#endif
