/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef MINI_FRAMEBUFFER_PIXELS_H
#define MINI_FRAMEBUFFER_PIXELS_H

/* Boot scanout is packed BGR888 on Sony, BGRA8888 on the reference panel.
 * Byte access also permits an odd 24bpp row stride without unaligned words.
 */
static inline uint32_t miniPanelRead(const uint8_t *row, uint32_t x, uint32_t depth)
{
    const uint8_t *p = row + (size_t)x * (depth / 8);
    uint32_t value = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    return depth == 32 ? value | (uint32_t)p[3] << 24 : value;
}

static inline void miniPanelWrite(uint8_t *row, uint32_t x, uint32_t depth, uint32_t value)
{
    uint8_t *p = row + (size_t)x * (depth / 8);
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16;
    if (depth == 32) p[3] = value >> 24;
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/* Four packed BGR pixels occupy three aligned words. Caller checks both
 * alignment and room for all four pixels; no row padding is touched. */
typedef uint32_t mini_alias_word __attribute__((__may_alias__));
static inline void miniPanelWrite4(uint8_t *p, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    mini_alias_word *out = (mini_alias_word *)p;
    out[0] = (a & 0xffffffu) | (b << 24);
    out[1] = ((b >> 8) & 0xffffu) | (c << 16);
    out[2] = ((c >> 16) & 0xffu) | (d << 8);
}
#endif

static inline void miniPanelClear(uint8_t *row, uint32_t width, uint32_t depth)
{
    bzero(row, (size_t)width * (depth / 8));
}
#endif
