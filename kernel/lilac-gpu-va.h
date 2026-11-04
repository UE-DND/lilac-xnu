/* SPDX-License-Identifier: BSD-2-Clause
 * A bounded GPU VA allocator. The caller serializes access. Allocation starts
 * record their exact extent; interior/double/incorrect-size frees fail closed.
 */
#ifndef LILAC_GPU_VA_H
#define LILAC_GPU_VA_H
#include <stdint.h>
#include <stddef.h>
#define LILAC_GPU_VA_START 0x04000000ull
#define LILAC_GPU_VA_END 0x40000000ull
#define LILAC_GPU_VA_GRANULE 65536u
#define LILAC_GPU_VA_SLOTS ((LILAC_GPU_VA_END-LILAC_GPU_VA_START)/LILAC_GPU_VA_GRANULE)
struct lilac_gpu_va { uint16_t slots[LILAC_GPU_VA_SLOTS]; };
static inline uint32_t lilac_gpu_va_count(uint64_t size)
{
    if (!size || size > LILAC_GPU_VA_END-LILAC_GPU_VA_START) return 0;
    return (uint32_t)((size+65535)/65536);
}
static inline uint64_t lilac_gpu_va_alloc(struct lilac_gpu_va *v, uint64_t size)
{
    uint32_t count = lilac_gpu_va_count(size), start = 0, free = 0;
    if (!v || !count) return 0;
    for (uint32_t i = 0; i < LILAC_GPU_VA_SLOTS; i++) {
        if (v->slots[i]) { free = 0; start = i+1; }
        else if (++free == count) {
            v->slots[start] = (uint16_t)count;
            for (uint32_t j = 1; j < count; j++) v->slots[start+j] = UINT16_MAX;
            return LILAC_GPU_VA_START+(uint64_t)start*65536;
        }
    }
    return 0;
}
static inline int lilac_gpu_va_free(struct lilac_gpu_va *v, uint64_t address, uint64_t size)
{
    uint32_t count = lilac_gpu_va_count(size);
    if (!v || !count || address < LILAC_GPU_VA_START || address >= LILAC_GPU_VA_END || (address & 65535)) return 0;
    uint32_t at = (uint32_t)((address-LILAC_GPU_VA_START)/65536);
    if (count > LILAC_GPU_VA_SLOTS-at || v->slots[at] != count) return 0;
    for (uint32_t i = 0; i < count; i++) v->slots[at+i] = 0;
    return 1;
}
static inline int lilac_gpu_va_contains(const struct lilac_gpu_va *v, uint64_t address, uint64_t size)
{
    if (!v || !size || address < LILAC_GPU_VA_START || address >= LILAC_GPU_VA_END || size > LILAC_GPU_VA_END-address) return 0;
    for (uint64_t at = (address-LILAC_GPU_VA_START)/65536; at <= (address+size-1-LILAC_GPU_VA_START)/65536; at++)
        if (!v->slots[at]) return 0;
    return 1;
}
#endif
