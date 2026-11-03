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

static inline void miniPanelClear(uint8_t *row, uint32_t width, uint32_t depth)
{
    bzero(row, (size_t)width * (depth / 8));
}
#endif
