/* SPDX-License-Identifier: BSD-2-Clause
 * Terminal Sony ABL handoff experiment, NOT a general UFS driver.
 * Only LUN 0 READ CAPACITY(10) and single-block READ(10) are emitted.
 * Firmware uses one TR descriptor at page+0, its UCD at page+0x100.
 * The adjacent, stopped TM list page is temporary data storage. This avoids
 * both a freed firmware data buffer and a new/unproven DMA mapping.
 * No queue base, UIC, interrupt, reset, clock, crypto or SMMU register writes.
 * Validate a retired stock READ/WRITE descriptor; never resubmit that command.
 * Batch complete GPT arrays and repeated partition boundary reads in one boot.
 * Restore both pages only once the transfer list has stopped with no DMA in
 * flight. On timeout leave memory intact, halt and require a hardware reboot.
 */
#include "ufs-read.h"
#include <stddef.h>

#define CAP 0x00
#define VER 0x08
#define IS 0x20
#define IE 0x24
#define HCS 0x30
#define HCE 0x34
#define TRBA 0x50
#define TRBAU 0x54
#define TRDB 0x58
#define TRRUN 0x60
#define TMBA 0x70
#define TMBAU 0x74
#define TMDB 0x78
#define TMRUN 0x80
#define FATAL ((1u<<2)|(1u<<11)|(1u<<12)|(1u<<16)|(1u<<17))
#define PAGE 4096u
#define UCD 0x100u
#define RESPONSE 0x120u
#define PRDT 0x160u

#if defined(LILAC_UFS_READ_TEST) || defined(LILAC_UFS_IO_EXTERNAL)
extern uint32_t ur_reg(uint32_t offset);
extern void ur_set_reg(uint32_t offset, uint32_t value);
extern uint32_t ur_load(uint64_t address);
extern void ur_store(uint64_t address, uint32_t value);
extern uint64_t ur_ticks(void);
extern uint64_t ur_frequency(void);
extern void ur_barrier(void);
#else
__attribute__((noinline)) static uint32_t ur_reg(uint32_t offset) {
    return *(const volatile uint32_t *)(0x01da4000ULL + offset);
}
__attribute__((noinline)) static void ur_set_reg(uint32_t offset, uint32_t value) {
    /* The only MMIO stores in this module. Fail closed for other offsets. */
    if (!((offset == TRRUN && value <= 1) || (offset == TRDB && value == 1)))
        for (;;) __asm__ volatile ("wfi");
    *(volatile uint32_t *)(0x01da4000ULL + offset) = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}
__attribute__((noinline)) static uint32_t ur_load(uint64_t address) { return *(const volatile uint32_t *)address; }
__attribute__((noinline)) static void ur_store(uint64_t address, uint32_t value) { *(volatile uint32_t *)address = value; }
static uint64_t ur_ticks(void) { uint64_t v; __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(v)); return v; }
static uint64_t ur_frequency(void) { uint64_t v; __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
static void ur_barrier(void) { __asm__ volatile ("dsb sy" ::: "memory"); }
#endif

/* All buffers belong to the loaded shim, never serve as DMA destinations. */
static uint32_t saved_tr[PAGE/4], saved_tm[PAGE/4];
static uint32_t primary[PAGE/4], data[PAGE/4];
static uint32_t entries[128*1024/4];
static uint64_t frequency;
static uint32_t tr, tm, command_type;
static struct ur_result *result;

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[3]<<24 | (uint32_t)p[2]<<16 | (uint32_t)p[1]<<8 | p[0];
}
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p+4)<<32 | le32(p); }
static void putbe32(uint8_t *p, uint32_t v) {
    p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v;
}
static int equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i=0; i<n; i++) if (a[i]!=b[i]) return 0;
    return 1;
}
static uint32_t crc_update(uint32_t crc, const uint8_t *p, uint32_t n, int header) {
    for (uint32_t i=0; i<n; i++) {
        crc ^= header && i>=16 && i<20 ? 0 : p[i];
        for (int b=0; b<8; b++) crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1)));
    }
    return crc;
}
static uint32_t crc32(const uint8_t *p, uint32_t n, int header) {
    return ~crc_update(~0u,p,n,header);
}
static void stage(uint32_t s, uint32_t detail) {
    result->stage=s;
    lilac_ufs_read_progress(s, detail);
    ur_barrier();
}
static int wait_reg(uint32_t reg, uint32_t mask, uint32_t expected, uint32_t ms) {
    uint64_t start=ur_ticks(), ticks=frequency*ms/1000;
    /* Iteration bound also handles a counter which ceases advancing. */
    for (uint32_t n=0; n<10000000; n++) {
        if ((ur_reg(reg)&mask)==expected) return 1;
        if (ur_ticks()-start>=ticks) break;
    }
    /* The caller can be descheduled after observing a busy register and
     * before checking the clock. Re-read once at the deadline: a completed
     * command still goes through the normal OCS, UPIU and queue checks.
     * If it is still busy, retain the existing no-cleanup timeout path. */
    return (ur_reg(reg)&mask)==expected;
}
static int same_queues(void) {
    return ur_reg(TRBA)==tr && ur_reg(TRBAU)==0 && ur_reg(TMBA)==tm &&
        ur_reg(TMBAU)==0 && ur_reg(TMRUN)==0 && ur_reg(TMDB)==0 &&
        ur_reg(IE)==0 && ur_reg(HCE)==1 && (ur_reg(HCS)&3)==3;
}
static void load_page(uint32_t *dst, uint32_t address) {
    for (uint32_t i=0;i<PAGE/4;i++) dst[i]=ur_load((uint64_t)address+i*4);
}
static void store_page(uint32_t address, const uint32_t *src) {
    for (uint32_t i=0;i<PAGE/4;i++) ur_store((uint64_t)address+i*4,src[i]);
}
static int page_equal(uint32_t address, const uint32_t *src) {
    for (uint32_t i=0;i<PAGE/4;i++) if (ur_load((uint64_t)address+i*4)!=src[i]) return 0;
    return 1;
}

/* The last firmware command is retired history, not a command template.
 * Require its direction/opcode, CDB, length, PRDT and successful response to
 * agree before borrowing the pages. Never dereference its old data pointer.
 * The separate permitted() whitelist below accepts generated READs only. */
static int retired_layout(const uint32_t *d, uint32_t base) {
    const uint8_t *u=(const uint8_t *)d+UCD, *v=(const uint8_t *)d+RESPONSE;
    uint32_t dw=d[0]&~0x10000000u;
    int read=dw==0x05000000u && u[1]==0x40 && u[16]==0x28 && u[17]==0;
    int write=dw==0x03000000u && u[1]==0x20 && u[16]==0x2a && u[17]==8;
    uint32_t blocks=(uint32_t)u[23]<<8|u[24], bytes=be32(u+12);
    uint32_t address=d[PRDT/4];
    if ((!read && !write) || d[1] || d[2] || d[3] || d[4]!=base+UCD || d[5] ||
        d[6]!=0x00080010 || d[7]!=0x00180001 || u[0]!=1 || u[2] ||
        !blocks || blocks>64 || bytes!=blocks*PAGE || u[22] ||
        d[PRDT/4+1] || d[PRDT/4+2] || d[PRDT/4+3]!=bytes-1 ||
        address<0x80000000u || address>0xa0000000u-bytes || (address&3) ||
        v[0]!=0x21 || v[1] || v[2] || v[3]!=u[3] || v[6] || v[7] ||
        v[10] || v[11] || be32(v+12)) return 0;
    for (uint32_t i=4;i<12;i++) if (u[i]) return 0;
    for (uint32_t i=25;i<32;i++) if (u[i]) return 0;
    return 1;
}

/* Independent wire whitelist: command type, flags, LUN, reserved bytes,
 * exact CDB, expected length and target range must all agree. */
static int permitted(const uint8_t *u, uint32_t dw0, uint32_t bytes,
                     uint32_t last, uint32_t block) {
    if ((dw0!=0x05000000 && dw0!=0x15000000) || u[0]!=1 || u[1]!=0x40 || u[2]!=0 ||
        be32(u+12)!=bytes) return 0;
    for (uint32_t i=4;i<12;i++) if (u[i]) return 0;
    if (u[16]==0x25) {
        if (bytes!=8) return 0;
        for (uint32_t i=17;i<32;i++) if (u[i]) return 0;
        return 1;
    }
    if (u[16]!=0x28 || block!=PAGE || bytes!=block || be32(u+18)>last ||
        u[17] || u[22] || u[23] || u[24]!=1) return 0;
    for (uint32_t i=25;i<32;i++) if (u[i]) return 0;
    return 1;
}

static int transfer_impl(uint8_t opcode, uint32_t lba, uint32_t bytes, int poison) {
    uint32_t words[8]={0}, descriptor[8]={0}, wire[8];
    uint8_t *u=(uint8_t *)words;
    result->failed_lba=lba;
    if (result->submitted>=UR_MAX_COMMANDS) { result->error=UR_LIMIT; return 0; }
    u[0]=1; u[1]=0x40; u[3]=(uint8_t)(0xc0+result->completed);
    putbe32(u+12,bytes); u[16]=opcode;
    if (opcode==0x28) { putbe32(u+18,lba); u[24]=1; }
    uint32_t dw0=command_type|0x05000000; /* inbound, interrupt command (IE remains 0) */
    if (!permitted(u,dw0,bytes,result->last_lba,result->block_bytes)) {
        result->error=UR_VERIFY_FAILED; return 0;
    }
    if (!same_queues() || ur_reg(TRDB)!=0 || ur_reg(TRRUN)!=1 || (ur_reg(IS)&FATAL)) {
        result->error=UR_BAD_STATE; return 0;
    }
    ur_set_reg(TRRUN,0);
    if (!wait_reg(TRRUN,1,0,100)) { result->error=UR_STOP_TIMEOUT; return 0; }
    if (!same_queues() || ur_reg(TRDB)!=0) { result->error=UR_BAD_STATE; return 0; }
    /* Poison is useful for the diagnostic probe. Completion is established by
     * the poisoned OCS/response and the checked UPIU, not the data pattern. */
    for (uint32_t i=0;i<0x80;i+=4) ur_store((uint64_t)tr+UCD+i,0);
    if (poison) for (uint32_t i=0;i<PAGE;i+=4) ur_store((uint64_t)tm+i,0xa5a5a5a5);
    for (uint32_t i=0;i<8;i++) ur_store((uint64_t)tr+UCD+i*4,words[i]);
    ur_store((uint64_t)tr+PRDT,tm);
    ur_store((uint64_t)tr+PRDT+12,bytes-1);
    descriptor[0]=dw0; descriptor[2]=0xf; descriptor[4]=tr+UCD;
    descriptor[6]=0x00080010; descriptor[7]=0x00180001;
    for (uint32_t i=0;i<8;i++) ur_store((uint64_t)tr+i*4,descriptor[i]);
    ur_barrier();
    for (uint32_t i=0;i<8;i++) {
        wire[i]=ur_load((uint64_t)tr+UCD+i*4);
        if (ur_load((uint64_t)tr+i*4)!=descriptor[i]) { result->error=UR_VERIFY_FAILED; return 0; }
    }
    if (!equal((uint8_t *)wire,u,32) || !permitted((uint8_t *)wire,dw0,bytes,result->last_lba,result->block_bytes) ||
        ur_load((uint64_t)tr+PRDT)!=tm || ur_load((uint64_t)tr+PRDT+4)!=0 ||
        ur_load((uint64_t)tr+PRDT+8)!=0 || ur_load((uint64_t)tr+PRDT+12)!=bytes-1) {
        result->error=UR_VERIFY_FAILED; return 0;
    }
    ur_set_reg(TRRUN,1);
    if (!wait_reg(TRRUN,1,1,100) || !same_queues() || ur_reg(TRDB)!=0) {
        result->error=UR_START_FAILED; return 0;
    }
    ur_barrier();
    result->submitted++;
    ur_set_reg(TRDB,1); /* The sole submission point; only slot 0. */
    if (!wait_reg(TRDB,~0u,0,2000)) { result->error=UR_IO_TIMEOUT; return 0; }
    ur_barrier();
    result->ocs=ur_load((uint64_t)tr+8);
    result->response=ur_load((uint64_t)tr+RESPONSE);
    result->is_after=ur_reg(IS);
    uint32_t reply[8];
    for (uint32_t i=0;i<8;i++) reply[i]=ur_load((uint64_t)tr+RESPONSE+i*4);
    for (uint32_t i=0;i<4;i++) result->response_words[i]=reply[i];
    uint8_t *resp=(uint8_t *)reply;
    if (result->ocs!=0 || resp[0]!=0x21 || resp[1]!=0 || resp[2]!=0 || resp[3]!=u[3] ||
        resp[6]!=0 || resp[7]!=0 || resp[10]!=0 || resp[11]!=0 || be32(resp+12)!=0 ||
        (result->is_after&FATAL) || !same_queues()) {
        result->error=UR_RESPONSE_FAILED; return 0;
    }
    load_page(data,tm);
    for (uint32_t i=0;i<4;i++) result->data_words[i]=data[i];
    result->completed++;
    return 1;
}

static int transfer(uint8_t opcode, uint32_t lba, uint32_t bytes) {
    return transfer_impl(opcode,lba,bytes,1);
}

static int valid_gpt(const uint8_t *p, uint32_t own, uint32_t other) {
    static const uint8_t signature[8]={'E','F','I',' ','P','A','R','T'};
    uint32_t size=le32(p+12);
    if (!equal(p,signature,8) || le32(p+8)!=0x10000 || size<92 || size>PAGE || le32(p+20)!=0 ||
        le64(p+24)!=own || le64(p+32)!=other || le64(p+40)>le64(p+48) ||
        le64(p+40)<2 || le64(p+48)>=result->last_lba ||
        le32(p+16)!=crc32(p,size,1)) return 0;
    uint64_t bytes=(uint64_t)le32(p+80)*le32(p+84), entry_lba=le64(p+72);
    if (!le32(p+80) || le32(p+80)>UR_MAX_PARTITIONS || le32(p+84)<128 || le32(p+84)%128 || bytes>sizeof(entries) ||
        entry_lba<2 || entry_lba>=result->last_lba) return 0;
    uint64_t blocks=(bytes+PAGE-1)/PAGE;
    if (own==1 ? entry_lba+blocks>le64(p+40) :
        (entry_lba<=le64(p+48) || entry_lba+blocks>result->last_lba)) return 0;
    return 1;
}

static int read_entries(uint32_t s, uint32_t lba, uint32_t bytes, int save,
                        uint32_t *measured) {
    uint32_t crc=~0u;
    uint8_t *dst=(uint8_t *)entries, *p=(uint8_t *)data;
    for (uint32_t off=0;off<bytes;off+=PAGE) {
        uint32_t n=bytes-off<PAGE?bytes-off:PAGE;
        stage(s,lba+off/PAGE);
        if (!transfer(0x28,lba+off/PAGE,PAGE)) return 0;
        crc=crc_update(crc,p,n,0);
        if (save) for (uint32_t i=0;i<n;i++) dst[off+i]=p[i];
        else if (!equal(dst+off,p,n)) { result->error=UR_UNSTABLE; return 0; }
    }
    *measured=~crc;
    if (*measured!=result->entries_crc) { result->error=UR_BAD_ENTRIES; return 0; }
    return 1;
}
static int nonzero(const uint8_t *p, uint32_t n) {
    for (uint32_t i=0;i<n;i++) if (p[i]) return 1;
    return 0;
}
static int inspect_partitions(void) {
    const uint8_t *all=(const uint8_t *)entries;
    for (uint32_t i=0;i<result->entries_count;i++) {
        const uint8_t *p=all+i*result->entry_bytes;
        if (!nonzero(p,16)) continue;
        uint64_t first=le64(p+32), last=le64(p+40);
        if (!nonzero(p+16,16) || first<result->first_usable || last>result->last_usable || first>last) {
            result->error=UR_BAD_PARTITION; return 0;
        }
        for (uint32_t j=0;j<i;j++) {
            const uint8_t *q=all+j*result->entry_bytes;
            if (nonzero(q,16) && (equal(p+16,q+16,16) ||
                (first<=le64(q+40) && last>=le64(q+32)))) {
                result->error=UR_BAD_PARTITION; return 0;
            }
        }
        struct ur_partition *part=&result->partition[result->partitions++];
        part->first=first; part->last=last;
        for (uint32_t k=0;k<36;k++) {
            uint32_t ch=(uint32_t)p[56+k*2]|(uint32_t)p[57+k*2]<<8;
            part->name[k]=ch==0?0:(ch>=32 && ch<=126?(char)ch:'?');
        }
        part->name[36]=0;
    }
    if (!result->partitions) { result->error=UR_BAD_PARTITION; return 0; }
    return 1;
}
static uint32_t signature(const uint8_t *p) {
    if (equal(p,(const uint8_t *)"ANDROID!",8)) return 1;
    if (equal(p+32,(const uint8_t *)"NXSB",4)) return 2;
    if (p[1080]==0x53 && p[1081]==0xef) return 3;
    if (le32(p)==0xed26ff3a) return 4; /* Android sparse image. */
    if (equal(p,(const uint8_t *)"AVB0",4)) return 5;
    if (p[0]==0xd0 && p[1]==0x0d && p[2]==0xfe && p[3]==0xed) return 6;
    return 0; /* Fingerprint only, not a filesystem validation. */
}
static int sample_partitions(void) {
    for (uint32_t i=0;i<result->partitions;i++) {
        struct ur_partition *p=&result->partition[i];
        stage(UR_PARTITIONS,(uint32_t)p->first);
        if (!transfer(0x28,(uint32_t)p->first,PAGE)) return 0;
        p->head_crc=crc32((uint8_t *)data,PAGE,0); p->signature=signature((uint8_t *)data);
        for (uint32_t k=0;k<PAGE/4;k++) primary[k]=data[k];
        if (!transfer(0x28,(uint32_t)p->first,PAGE)) return 0;
        if (!equal((uint8_t *)primary,(uint8_t *)data,PAGE)) { result->error=UR_UNSTABLE; return 0; }
        stage(UR_PARTITIONS,(uint32_t)p->last);
        if (!transfer(0x28,(uint32_t)p->last,PAGE)) return 0;
        p->tail_crc=crc32((uint8_t *)data,PAGE,0);
        result->sampled++;
    }
    return 1;
}

static int begin(struct ur_result *r) {
    result=r;
    uint8_t *zero=(uint8_t *)r;
    for (size_t i=0;i<sizeof(*r);i++) zero[i]=0;
    stage(UR_PREFLIGHT,0);
    frequency=ur_frequency();
    r->cap=ur_reg(CAP); r->version=ur_reg(VER); r->hcs=ur_reg(HCS);
    r->is_before=ur_reg(IS);
    tr=r->tr_base=ur_reg(TRBA); tm=r->tm_base=ur_reg(TMBA);
    if (!frequency || frequency>1000000000 || r->version!=0x210 || r->cap==0 || r->cap==~0u ||
        tr<0x99000000 || tr>0x9b000000-PAGE*2 || (tr&(PAGE-1)) || tm!=tr+PAGE ||
        !same_queues() || ur_reg(TRDB)!=0 || ur_reg(TRRUN)!=1 || (r->is_before&FATAL)) {
        r->error=UR_BAD_STATE; return 0;
    }
    stage(UR_LAYOUT,tr);
    load_page(saved_tr,tr); load_page(saved_tm,tm);
    const uint8_t *old=(uint8_t *)saved_tr+UCD;
    r->old_dw0=saved_tr[0]; r->old_cdb=old[16];
    command_type=saved_tr[0]&0x10000000;
    if (!retired_layout(saved_tr,tr) || !same_queues() || ur_reg(TRDB)!=0 || ur_reg(TRRUN)!=1 ||
        !page_equal(tr,saved_tr) || !page_equal(tm,saved_tm)) {
        r->error=UR_BAD_LAYOUT; return 0;
    }
    return 1;
}

static void finish(struct ur_result *r, uint64_t started) {
    r->doorbell=ur_reg(TRDB); r->tr_run=ur_reg(TRRUN);
    r->elapsed_ms=(uint32_t)((ur_ticks()-started)*1000/frequency);
    /* A timeout may leave DMA active: no cleanup writes, restart or reset. */
    if (r->error==UR_IO_TIMEOUT || !same_queues() || ur_reg(TRDB)!=0) return;
    uint32_t failed_stage=r->stage;
    stage(UR_RESTORE,tr);
    ur_set_reg(TRRUN,0);
    if (!wait_reg(TRRUN,1,0,100) || ur_reg(TRDB)!=0 || !same_queues()) { r->error=UR_RESTORE_FAILED; return; }
    /* Commands change only the slot-0 descriptor and its UCD/PRDT. Do not
     * rewrite the untouched remainder of the firmware transfer page. Verify
     * the entire page below so any unexpected modification still fails. */
    for (uint32_t i=0;i<8;i++) ur_store((uint64_t)tr+i*4,saved_tr[i]);
    for (uint32_t i=UCD/4;i<(UCD+0x80)/4;i++) ur_store((uint64_t)tr+i*4,saved_tr[i]);
    store_page(tm,saved_tm); ur_barrier();
    if (!page_equal(tr,saved_tr) || !page_equal(tm,saved_tm)) { r->error=UR_RESTORE_FAILED; return; }
    ur_set_reg(TRRUN,1);
    if (!wait_reg(TRRUN,1,1,100) || !same_queues() || ur_reg(TRDB)!=0) { r->error=UR_RESTORE_FAILED; return; }
    r->restored=1;
    if (r->error) stage(failed_stage,r->error);
    else stage(UR_DONE,0);
}

static void read_probe(struct ur_result *r, int sample) {
    if (!begin(r)) return;
    uint64_t started=ur_ticks();
    stage(UR_CAPACITY,0);
    if (!transfer(0x25,0,8)) goto restore;
    r->last_lba=be32((uint8_t *)data); r->block_bytes=be32((uint8_t *)data+4);
    if (r->last_lba==~0u || r->last_lba<64 || r->block_bytes!=PAGE) { r->error=UR_BAD_CAPACITY; goto restore; }
    r->checks|=UR_CHECK_CAPACITY;
    stage(UR_MBR,0);
    if (!transfer(0x28,0,PAGE)) goto restore;
    const uint8_t *p=(uint8_t *)data;
    r->mbr_crc=crc32(p,PAGE,0);
    if (p[510]!=0x55 || p[511]!=0xaa) { r->error=UR_BAD_MBR; goto restore; }
    r->checks|=UR_CHECK_MBR;
    stage(UR_PRIMARY,1);
    if (!transfer(0x28,1,PAGE)) goto restore;
    if (!valid_gpt(p,1,r->last_lba)) { r->error=UR_BAD_GPT; goto restore; }
    for (uint32_t i=0;i<PAGE/4;i++) primary[i]=data[i];
    r->primary_crc=crc32(p,PAGE,0); r->header_crc=le32(p+16);
    r->entries_crc=le32(p+88); r->entries_count=le32(p+80); r->entry_bytes=le32(p+84);
    r->first_usable=le64(p+40); r->last_usable=le64(p+48);
    stage(UR_REPEAT,1);
    if (!transfer(0x28,1,PAGE)) goto restore;
    if (!equal((uint8_t *)primary,p,PAGE)) { r->error=UR_UNSTABLE; goto restore; }
    stage(UR_BACKUP,r->last_lba);
    if (!transfer(0x28,r->last_lba,PAGE)) goto restore;
    r->backup_crc=crc32(p,PAGE,0);
    if (!valid_gpt(p,r->last_lba,1) || !equal(p+40,(uint8_t *)primary+40,32) ||
        !equal(p+80,(uint8_t *)primary+80,12)) { r->error=UR_BAD_GPT; goto restore; }
    r->checks|=UR_CHECK_HEADERS;
    uint32_t bytes=r->entries_count*r->entry_bytes;
    uint32_t primary_lba=(uint32_t)le64((uint8_t *)primary+72), backup_lba=(uint32_t)le64(p+72);
    if (!read_entries(UR_ENTRIES,primary_lba,bytes,1,&r->primary_entries_crc) ||
        !read_entries(UR_BACKUP_ENTRIES,backup_lba,bytes,0,&r->backup_entries_crc) ||
        !read_entries(UR_REPEAT_ENTRIES,primary_lba,bytes,0,&r->repeat_entries_crc)) goto restore;
    r->checks|=UR_CHECK_ENTRIES;
    if (!inspect_partitions()) goto restore;
    if (sample) {
        if (!sample_partitions()) goto restore;
        r->checks|=UR_CHECK_PARTITIONS;
    }
    stage(UR_FINAL,0);
    if (!transfer(0x25,0,8)) goto restore;
    if (be32(p)!=r->last_lba || be32(p+4)!=r->block_bytes) { r->error=UR_UNSTABLE; goto restore; }
    if (!transfer(0x28,0,PAGE)) goto restore;
    if (crc32(p,PAGE,0)!=r->mbr_crc) { r->error=UR_UNSTABLE; goto restore; }
    if (!transfer(0x28,1,PAGE)) goto restore;
    if (crc32(p,PAGE,0)!=r->primary_crc) { r->error=UR_UNSTABLE; goto restore; }
    r->checks|=UR_CHECK_FINAL;
restore:
    finish(r,started);
}

void lilac_ufs_read_probe(struct ur_result *r) { read_probe(r,1); }
void lilac_ufs_read_probe_fast(struct ur_result *r) { read_probe(r,0); }

/* Serialized synchronous kernel adapter: scope each bounded read to a fresh
 * retired-layout snapshot and restore the original firmware pages on success.
 * A caller must permanently retire the device if restoration/IO fails. */
void lilac_ufs_read_range(struct ur_result *r, uint32_t last, uint32_t lba,
                         uint32_t count, uint8_t *dst) {
    if (!dst || !count || count>64 || last==~0u || lba>last || count-1>last-lba) {
        uint8_t *zero=(uint8_t *)r;
        for (size_t i=0;i<sizeof(*r);i++) zero[i]=0;
        r->error=UR_BAD_CAPACITY; return;
    }
    if (!begin(r)) return;
    uint64_t started=ur_ticks();
    r->block_bytes=PAGE; r->last_lba=last;
    stage(UR_PARTITIONS,lba);
    for (uint32_t i=0;i<count;i++) {
        if (!transfer_impl(0x28,lba+i,PAGE,0)) break;
        const uint8_t *src=(const uint8_t *)data;
        for (uint32_t j=0;j<PAGE;j++) dst[i*PAGE+j]=src[j];
    }
    finish(r,started);
}
