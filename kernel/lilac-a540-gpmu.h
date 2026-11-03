/* SPDX-License-Identifier: GPL-2.0-only
 * A540 GPMU fw2 -> PM4 type 4 stream, based on Linux a5xx_gpmu_ucode_init.
 * This prepares caller-owned memory only; it does not start a command processor.
 */
#ifndef LILAC_A540_GPMU_H
#define LILAC_A540_GPMU_H
#include "lilac-a5xx-pm4.h"
#define LILAC_A540_GPMU_INST_BASE 0x8800u
#define LILAC_A540_GPMU_INST_WORDS 0x1000u
static inline uint32_t lilac_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
/* Input and output must not overlap. Invalid input/capacity leaves output untouched. */
static inline int lilac_a540_gpmu_stream(const uint8_t *fw,size_t bytes,uint32_t *out,size_t capacity,size_t *written) {
    if(!fw || !out || !written || bytes<12 || bytes%4)return 0;
    size_t words=bytes/4;uint32_t len=lilac_read_le32(fw),id=lilac_read_le32(fw+4),skip=lilac_read_le32(fw+8);
    if(len<2 || len!=(words-1) || id!=2 || skip>len-2)return 0;
    size_t code=(size_t)len-skip-2,start=(size_t)skip+3;
    if(!code || code>LILAC_A540_GPMU_INST_WORDS || start>words || code>words-start)return 0;
    size_t needed=code+(code+126)/127;
    if(needed>capacity)return 0;
    size_t at=0,offset=0;
    while(offset<code) {
        size_t n=code-offset<127?code-offset:127;
        uint32_t reg=LILAC_A540_GPMU_INST_BASE+(uint32_t)offset;
        out[at++]=0x40000000u | (uint32_t)n | lilac_pm4_parity((uint32_t)n)<<7 |
            reg<<8 | lilac_pm4_parity(reg)<<27;
        for(size_t i=0;i<n;i++)out[at++]=lilac_read_le32(fw+4*(start+offset+i));
        offset+=n;
    }
    *written=at;return 1;
}
#endif
