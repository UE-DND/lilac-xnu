/* SPDX-License-Identifier: GPL-2.0-only
 * Single-ring A5xx indirect-buffer and completion packet construction.
 * Caller owns the ring lock, GPU VA mappings and cache maintenance; it must
 * publish the completed ring to the device before writing CP_RB_WPTR.
 */
#ifndef LILAC_A5XX_SUBMIT_H
#define LILAC_A5XX_SUBMIT_H
#include "lilac-a5xx-ring.h"

#define LILAC_A5XX_CP_INDIRECT_BUFFER_PFE 0x3fu
#define LILAC_A5XX_CP_EVENT_WRITE 0x46u
#define LILAC_A5XX_CACHE_FLUSH_TS 4u
#define LILAC_A5XX_SUBMIT_MAX 128u
#define LILAC_A5XX_VA_LIMIT (1ull << 39)

struct lilac_a5xx_ib { uint64_t address; uint32_t dwords; };

/* No bytes or write index change when inputs are invalid or space is short.
 * Fence memory must be a writable GPU mapping with normal cache protocol. */
static inline int lilac_a5xx_submit(struct lilac_a5xx_ring *ring, size_t read_index,
    const struct lilac_a5xx_ib *ibs, size_t count, uint64_t fence_address, uint32_t sequence)
{
    if (!ring || !ring->words || ring->count < 2 || (ring->count & (ring->count - 1)) ||
        ring->write_index >= ring->count || read_index >= ring->count || !ibs ||
        !count || count > LILAC_A5XX_SUBMIT_MAX || !sequence || !fence_address ||
        (fence_address & 3) || fence_address >= LILAC_A5XX_VA_LIMIT ||
        fence_address > LILAC_A5XX_VA_LIMIT - 4) return 0;
    struct lilac_a5xx_ib submitted[LILAC_A5XX_SUBMIT_MAX];
    for (size_t i = 0; i < count; i++) {
        submitted[i] = ibs[i];
        uint64_t bytes = (uint64_t)submitted[i].dwords * 4;
        if (!submitted[i].address || (submitted[i].address & 3) || !submitted[i].dwords ||
            submitted[i].address >= LILAC_A5XX_VA_LIMIT || bytes > LILAC_A5XX_VA_LIMIT - submitted[i].address)
            return 0;
    }
    size_t needed = count * 4 + 5;
    size_t free_words = (read_index - ring->write_index - 1) & (ring->count - 1);
    if (needed > free_words) return 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t payload[] = {(uint32_t)submitted[i].address, (uint32_t)(submitted[i].address >> 32), submitted[i].dwords};
        if (!lilac_a5xx_ring_packet7(ring, read_index, LILAC_A5XX_CP_INDIRECT_BUFFER_PFE, payload, 3)) return 0;
    }
    uint32_t fence[] = {LILAC_A5XX_CACHE_FLUSH_TS, (uint32_t)fence_address,
                        (uint32_t)(fence_address >> 32), sequence};
    return lilac_a5xx_ring_packet7(ring, read_index, LILAC_A5XX_CP_EVENT_WRITE, fence, 4);
}
#endif
