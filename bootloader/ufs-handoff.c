/* SPDX-License-Identifier: BSD-2-Clause
 * Observe the inherited slot after ExitBootServices, before entering XNU.
 * No MMIO stores, no writes to firmware RAM and no storage commands.
 * Queue pointers are checked before use. Old PRD data pointers are displayed
 * as raw words only, never dereferenced. Sampling cannot prove DMA ownership.
 */
#include "ufs-handoff.h"
#include <stddef.h>

#define UH_PAGE 4096u
#define UH_UCD 0x100u
#define UH_RESPONSE 0x120u
#define UH_PRDT 0x160u
#define UH_FATAL ((1u<<2)|(1u<<11)|(1u<<12)|(1u<<16)|(1u<<17))
static const uint32_t offsets[UH_REGISTER_COUNT] = {
    0x00, 0x08, 0x30, 0x34, 0x20, 0x24, 0x50, 0x54,
    0x70, 0x74, 0x58, 0x60, 0x78, 0x80
};

#ifdef LILAC_UFS_HANDOFF_TEST
extern uint32_t uh_load(uint64_t address);
extern void uh_barrier(void);
#else
__attribute__((noinline)) static uint32_t uh_load(uint64_t address) {
    return *(const volatile uint32_t *)address;
}
static void uh_barrier(void) { __asm__ volatile ("dsb sy" ::: "memory"); }
#endif

/* Local copies are part of the loaded shim, never used as DMA destinations. */
static uint32_t captured_pages[2][UH_PAGE/4];

static uint32_t read_word(uint32_t stage, uint32_t address) {
    lilac_ufs_handoff_progress(stage,address);
    uh_barrier();
    return uh_load(address);
}
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint32_t page_crc(const uint32_t *page) {
    const uint8_t *p=(const uint8_t *)page;
    uint32_t crc=~0u;
    for (uint32_t i=0;i<UH_PAGE;i++) {
        crc^=p[i];
        for (uint32_t bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    return ~crc;
}
static void copy_words(uint32_t *dst, const uint32_t *src, uint32_t n) {
    for (uint32_t i=0;i<n;i++) dst[i]=src[i];
}
static uint32_t legacy_read_guard(const struct uh_result *r) {
    /* Report the existing read probe's layout predicates as a bitmask.
     * This diagnostic never relaxes them and never submits a command. */
    const uint32_t *d=r->descriptor;
    const uint8_t *u=(const uint8_t *)r->request;
    const uint8_t *v=(const uint8_t *)r->response;
    uint32_t mask=0;
    if (d[0]!=0x05000000 && d[0]!=0x15000000) mask|=1u<<0;
    if (d[1] || d[2] || d[3]) mask|=1u<<1;
    if (d[4]!=r->reg[UH_TRBA]+UH_UCD || d[5]) mask|=1u<<2;
    if (d[6]!=0x00080010 || d[7]!=0x00180001) mask|=1u<<3;
    if (u[0]!=1 || u[1]!=0x40 || u[2] || u[16]!=0x28) mask|=1u<<4;
    if (v[0]!=0x21 || v[3]!=u[3] || v[6] || v[7]) mask|=1u<<5;
    if (!r->stable) mask|=1u<<6;
    return mask;
}

void lilac_ufs_handoff_snapshot(struct uh_result *r) {
    uint8_t *zero=(uint8_t *)r;
    for (size_t i=0;i<sizeof(*r);i++) zero[i]=0;
    for (uint32_t i=0;i<UH_REGISTER_COUNT;i++)
        r->reg[i]=read_word(1,0x01da4000u+offsets[i]);
    uint32_t tr=r->reg[UH_TRBA], tm=r->reg[UH_TMBA];
    if (r->reg[UH_CAP]==0 || r->reg[UH_CAP]==~0u || r->reg[UH_VER]!=0x210 ||
        tr<0x99000000u || tr>0x9b000000u-UH_PAGE*2 || (tr&(UH_PAGE-1)) || tm!=tr+UH_PAGE ||
        r->reg[UH_TRBAU] || r->reg[UH_TMBAU] || r->reg[UH_TRDB] || r->reg[UH_TMDB] ||
        r->reg[UH_TRRUN]!=1 || r->reg[UH_TMRUN] || r->reg[UH_IE] || r->reg[UH_HCE]!=1 ||
        (r->reg[UH_HCS]&3)!=3 || (r->reg[UH_IS]&UH_FATAL)) {
        r->error=UH_BAD_STATE; return;
    }
    for (uint32_t page=0;page<2;page++) {
        uint32_t base=page?tm:tr;
        for (uint32_t i=0;i<UH_PAGE/4;i++)
            captured_pages[page][i]=read_word(2+page,base+i*4);
    }
    copy_words(r->descriptor,captured_pages[0],8);
    copy_words(r->request,captured_pages[0]+UH_UCD/4,8);
    copy_words(r->response,captured_pages[0]+UH_RESPONSE/4,8);
    copy_words(r->prdt,captured_pages[0]+UH_PRDT/4,4);
    r->captured=1;
    r->tr_crc=page_crc(captured_pages[0]); r->tm_crc=page_crc(captured_pages[1]);
    r->stable=1;
    for (uint32_t page=0;page<2;page++) {
        uint32_t base=page?tm:tr;
        for (uint32_t i=0;i<UH_PAGE/4;i++)
            if (read_word(4+page,base+i*4)!=captured_pages[page][i]) r->stable=0;
    }
    /* Recheck controller state after the second RAM pass. IS may gain status
     * during unrelated events: expose any difference, rather than hide it. */
    for (uint32_t i=0;i<UH_REGISTER_COUNT;i++)
        if (read_word(6,0x01da4000u+offsets[i])!=r->reg[i]) r->stable=0;
    const uint8_t *u=(const uint8_t *)r->request, *v=(const uint8_t *)r->response;
    r->direction=(r->descriptor[0]>>25)&3;
    r->opcode=u[16]; r->lun=u[2]; r->tag=u[3];
    r->lba=be32(u+18); r->blocks=(uint32_t)u[23]<<8|u[24];
    r->bytes=be32(u+12); r->scsi_status=v[7];
    r->read_guard=legacy_read_guard(r);
    r->error=r->stable?UH_OK:UH_CHANGED;
    lilac_ufs_handoff_progress(7,0);
    uh_barrier();
}
