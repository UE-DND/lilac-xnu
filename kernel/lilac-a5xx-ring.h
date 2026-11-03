/* SPDX-License-Identifier: GPL-2.0-only
 * Bounded A5xx PM4 ring preparation. Caller owns serialization and must publish
 * memory to the GPU before writing a hardware doorbell. Payload must not
 * overlap ring storage, and read_index must be a synchronized hardware value.
 * This code does no I/O.
 */
#ifndef LILAC_A5XX_RING_H
#define LILAC_A5XX_RING_H
#include "lilac-a5xx-pm4.h"

struct lilac_a5xx_ring { uint32_t *words; size_t count, write_index; };
static inline int lilac_a5xx_ring_packet7(struct lilac_a5xx_ring *ring,size_t read_index,
                                          uint32_t opcode,const uint32_t *payload,size_t n) {
    uint32_t header;struct lilac_pm4 packet={&header,1,0};
    if(!ring || !ring->words || ring->count<2 || (ring->count&(ring->count-1)) ||
       ring->write_index>=ring->count || read_index>=ring->count || n>0x3fff || (n && !payload))return 0;
    if(!lilac_pm4_type7(&packet,opcode,NULL,0))return 0;
    header=0x70000000u | (uint32_t)n | (lilac_pm4_parity((uint32_t)n)<<15) |
        (opcode<<16) | (lilac_pm4_parity(opcode)<<23);
    size_t free_words=(read_index-ring->write_index-1)&(ring->count-1);
    if(n+1>free_words)return 0;
    size_t at=ring->write_index;ring->words[at]=header;at=(at+1)&(ring->count-1);
    for(size_t i=0;i<n;i++){ring->words[at]=payload[i];at=(at+1)&(ring->count-1);}
    ring->write_index=at;
    return 1;
}
#endif
