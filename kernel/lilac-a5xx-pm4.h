/* SPDX-License-Identifier: GPL-2.0-only
 * A5xx type 4/type 7 PM4 packet encoding, following Linux adreno_gpu.h.
 * This only builds words in caller-owned RAM; it cannot access or start a GPU.
 */
#ifndef LILAC_A5XX_PM4_H
#define LILAC_A5XX_PM4_H
#include <stdint.h>
#include <stddef.h>

struct lilac_pm4 { uint32_t *words; size_t capacity, used; };
static inline uint32_t lilac_pm4_parity(uint32_t v) {
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4;
    return (0x9669u >> (v & 15)) & 1;
}
static inline int lilac_pm4_type4(struct lilac_pm4 *b,uint32_t reg,const uint32_t *payload,size_t n) {
    if(!b || !b->words || !payload || !n || n>127 || reg>0x3ffff || b->used>b->capacity || n+1>b->capacity-b->used)return 0;
    b->words[b->used++]=0x40000000u | (uint32_t)n | (lilac_pm4_parity((uint32_t)n)<<7) |
        (reg<<8) | (lilac_pm4_parity(reg)<<27);
    for(size_t i=0;i<n;i++)b->words[b->used++]=payload[i];
    return 1;
}
static inline int lilac_pm4_type7(struct lilac_pm4 *b,uint32_t opcode,const uint32_t *payload,size_t n) {
    if(!b || !b->words || (n && !payload) || n>0x3fff || opcode>0x7f || b->used>b->capacity || n+1>b->capacity-b->used)return 0;
    b->words[b->used++]=0x70000000u | (uint32_t)n | (lilac_pm4_parity((uint32_t)n)<<15) |
        (opcode<<16) | (lilac_pm4_parity(opcode)<<23);
    for(size_t i=0;i<n;i++)b->words[b->used++]=payload[i];
    return 1;
}
#endif
