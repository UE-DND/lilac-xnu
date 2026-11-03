/* SPDX-License-Identifier: GPL-2.0-only
 * A540 CP_ME_INIT packet, Linux a5xx_me_init at bfdc0edd11f9.
 * This only queues words; the caller must already have powered and mapped
 * the GPU, initialized the CP/ring and made its firmware available. It must
 * publish the ring and wait for idle or handle a timeout after this returns.
 */
#ifndef LILAC_A540_ME_H
#define LILAC_A540_ME_H
#include "lilac-a5xx-ring.h"
#define LILAC_A5XX_CP_ME_INIT 0x48u
static inline int lilac_a540_me_init(struct lilac_a5xx_ring *ring, size_t read_index)
{
    static const uint32_t payload[8]={0x2f,3,0x20000000,0,0,0,0,0};
    return lilac_a5xx_ring_packet7(ring,read_index,LILAC_A5XX_CP_ME_INIT,payload,8);
}
#endif
