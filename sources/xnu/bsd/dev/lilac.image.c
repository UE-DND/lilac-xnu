/* SPDX-License-Identifier: BSD-2-Clause
 * Translate an immutable, host-validated plaintext file's FIEMAP. Coordinates
 * are 4KiB blocks, with physical addresses absolute on LUN0. No disk writes.
 */
#include "lilac-image.h"
int lilac_image_validate(const struct lilac_image_map *m,size_t bytes,
                        uint64_t first,uint64_t last,uint32_t crc) {
    if (bytes<sizeof(*m) || m->magic!=LILAC_IMAGE_MAGIC || m->version!=1 ||
        !m->count || m->count>LILAC_IMAGE_MAX_EXTENTS || m->reserved ||
        bytes!=sizeof(*m)+(size_t)m->count*sizeof(m->extent[0]) ||
        !m->image_bytes || (m->image_bytes&4095) || first>last ||
        m->userdata_first!=first || m->userdata_last!=last || m->gpt_crc!=crc) return 0;
    uint64_t end=0,total=m->image_bytes/4096;
    for (uint32_t i=0;i<m->count;i++) {
        const struct lilac_image_extent *e=&m->extent[i];
        if (e->logical!=end || !e->blocks || e->blocks>total-end ||
            e->physical<first || e->physical>last || e->blocks-1>last-e->physical) return 0;
        for (uint32_t j=0;j<i;j++) {
            const struct lilac_image_extent *q=&m->extent[j];
            if (e->physical<=q->physical+q->blocks-1 && q->physical<=e->physical+e->blocks-1) return 0;
        }
        end+=e->blocks;
    }
    return end==total;
}
int lilac_image_translate(const struct lilac_image_map *m,uint64_t block,uint64_t *physical) {
    if (!m || !physical || block>=m->image_bytes/4096) return 0;
    uint32_t lo=0,hi=m->count;
    while (lo<hi) {
        uint32_t mid=lo+(hi-lo)/2;const struct lilac_image_extent *e=&m->extent[mid];
        if (block<e->logical) hi=mid;
        else if (block-e->logical>=e->blocks) lo=mid+1;
        else {*physical=e->physical+block-e->logical;return 1;}
    }
    return 0;
}
