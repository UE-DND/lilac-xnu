/* SPDX-License-Identifier: BSD-2-Clause
 * Bare-metal XNU Mach-O Unpacker & Handoff for Sony Xperia XZ1 Compact
 *
 * Implements Apple Device Tree generation, XNULDRST return stub hook,
 * Framebuffer text/log rendering, and exact virtual/physical slide mapping.
 *
 * Compiled with -mstrict-align -mgeneral-regs-only because with MMU off
 * (SCTLR_EL1.M == 0), ARMv8 treats all data accesses as Device-nGnRnE.
 */

#include <stdint.h>
#include <stddef.h>
#include "boot_args.h"
#include "font_8x16.h"
#ifdef LILAC_USB_DEBUG
#include "debug-usb.h"
#endif
#ifdef LILAC_SMP
#include "smp-return.h"
static uint32_t lent_mpidrs[LILAC_SMP_MAX_CPUS];
static unsigned lent_count;
static int64_t shim_affinity(uint32_t mpidr, void *context) {
    (void)context;
    register uint64_t x0 __asm__("x0")=0xc4000004u;
    register uint64_t x1 __asm__("x1")=mpidr;
    register uint64_t x2 __asm__("x2")=0;
    register uint64_t x3 __asm__("x3")=0;
    __asm__ volatile("smc #0" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3) : :
                     "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
    return (int64_t)x0;
}
#endif
#ifdef LILAC_UFS_READ
#include "ufs-read.h"
#endif
#ifdef LILAC_UFS_HANDOFF
#include "ufs-handoff.h"
#endif

#ifndef LILAC_EXTRA_BOOT_ARGS
#define LILAC_EXTRA_BOOT_ARGS ""
#endif
#ifdef LILAC_SMP
#define LILAC_CPU_ARG "cpus=8 "
#else
#define LILAC_CPU_ARG "cpus=1 "
#endif
#ifdef LILAC_QUIET_BOOT
#define LILAC_RAMDISK_BOOT_ARGS \
    "debug=0x8 serial=0 " LILAC_CPU_ARG "rd=md0 " LILAC_EXTRA_BOOT_ARGS
#else
#define LILAC_RAMDISK_BOOT_ARGS \
    "debug=0x8 serial=0 " LILAC_CPU_ARG "rd=md0 hello_wait=300 lilac_trace=1 -v " LILAC_EXTRA_BOOT_ARGS
#endif
_Static_assert(sizeof(LILAC_RAMDISK_BOOT_ARGS) <= BOOT_LINE_LENGTH,
    "RAMDisk boot arguments exceed boot_args capacity");

#define KERNEL_VIRT_BASE   0xFFFFFFF004000000ULL
#define PHYS_BASE          0xa0000000ULL
#define MEM_SIZE           0x58000000ULL /* 1.375 GB RAM window (0xa0000000..0xf8000000, avoiding top firmware carveouts) */

#define FB_BASE            0x9d400000ULL
#define FB_WIDTH           720
#define FB_HEIGHT          1280

#define NVRAM_BANK_SIZE    0x2000u
#define PANIC_LOG_SIZE     0x80000u
#define PANIC_LOG_BASE     0x83000000ULL

#define LC_SEGMENT_64      0x19
#define LC_UNIXTHREAD      0x5

extern void shim_return_stub(void);

struct lilac_payload {
    uint32_t magic;      /* 0x4c494c43 "LILC" */
    uint32_t kernel_sz;
    uint32_t rd_sz;
    uint32_t reserved;
};

struct mach_header_64 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

static size_t str_len(const char *s) {
    size_t l = 0;
    while (*s++) l++;
    return l;
}

__attribute__((noinline))
static void *shim_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 7ULL) == 0) {
        while (n >= 8) {
            *(volatile uint64_t *)d = *(const volatile uint64_t *)s;
            d += 8;
            s += 8;
            n -= 8;
        }
    }
    while (n--) {
        *(volatile uint8_t *)d++ = *(const volatile uint8_t *)s++;
    }
    return dest;
}

__attribute__((noinline))
static void *shim_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    uint8_t b = (uint8_t)c;
    if (((uintptr_t)p & 7ULL) == 0 && b == 0) {
        while (n >= 8) {
            *(volatile uint64_t *)p = 0;
            p += 8;
            n -= 8;
        }
    }
    while (n--) {
        *(volatile uint8_t *)p++ = b;
    }
    return s;
}

static void flush_dcache(uint64_t start, uint64_t end) {
    uint64_t ctr;
    __asm__ volatile ("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = 4ULL << ((ctr >> 16) & 0xF);
    for (uint64_t p = start & ~(line - 1); p < end; p += line) {
        __asm__ volatile ("dc civac, %0" : : "r" (p));
    }
    __asm__ volatile ("dsb sy");
}

/* 24-bit BGR pixel helper */
static void draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;
    volatile uint8_t *fb24 = (volatile uint8_t *)(FB_BASE + (uint64_t)(y * FB_WIDTH + x) * 3ULL);
    fb24[0] = b;
    fb24[1] = g;
    fb24[2] = r;
}

static void fill_rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FB_WIDTH) x1 = FB_WIDTH;
    if (y1 > FB_HEIGHT) y1 = FB_HEIGHT;
    for (int y = y0; y < y1; y++) {
        volatile uint8_t *row = (volatile uint8_t *)(FB_BASE + (uint64_t)(y * FB_WIDTH + x0) * 3ULL);
        for (int x = x0; x < x1; x++) {
            row[0] = b;
            row[1] = g;
            row[2] = r;
            row += 3;
        }
    }
}

/* 2x scaled character (16x32) for main status headers */
static void draw_char(int cx, int cy, char ch, uint8_t r, uint8_t g, uint8_t b) {
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *bitmap = font_8x16[(int)(ch - 32)];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                draw_pixel(cx + col * 2,     cy + row * 2,     r, g, b);
                draw_pixel(cx + col * 2 + 1, cy + row * 2,     r, g, b);
                draw_pixel(cx + col * 2,     cy + row * 2 + 1, r, g, b);
                draw_pixel(cx + col * 2 + 1, cy + row * 2 + 1, r, g, b);
            }
        }
    }
}

/* 1x bold character (9x18 cell) for dense kernel log output (~74 cols x 45 rows) */
static void draw_char_small(int cx, int cy, char ch, uint8_t r, uint8_t g, uint8_t b) {
    if (ch < 32 || ch > 126) ch = '.';
    const uint8_t *bitmap = font_8x16[(int)(ch - 32)];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                draw_pixel(cx + col,     cy + row, r, g, b);
                draw_pixel(cx + col + 1, cy + row, r, g, b);
            }
        }
    }
}

static void draw_string(int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b) {
    int cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 34;
            cur_x = x;
        } else {
            draw_char(cur_x, y, *str, r, g, b);
            cur_x += 18;
        }
        str++;
    }
}

static void draw_string_small(int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b) {
    int cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 18;
            cur_x = x;
        } else {
            draw_char_small(cur_x, y, *str, r, g, b);
            cur_x += 9;
        }
        str++;
    }
}

static void format_hex(char *buf, const char *prefix, uint64_t val) {
    size_t i = 0;
    while (*prefix && i < 30) {
        *(volatile char *)(buf + i) = *prefix++;
        i++;
    }
    *(volatile char *)(buf + i++) = '0';
    *(volatile char *)(buf + i++) = 'x';
    static const char hex_chars[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        *(volatile char *)(buf + i++) = hex_chars[(val >> shift) & 0xf];
    }
    *(volatile char *)(buf + i) = '\0';
}

static void draw_hex(int x, int y, const char *prefix, uint64_t val, uint8_t r, uint8_t g, uint8_t b) {
    char buf[64];
    format_hex(buf, prefix, val);
    draw_string(x, y, buf, r, g, b);
}

static void draw_hex_small(int x, int y, const char *prefix, uint64_t val, uint8_t r, uint8_t g, uint8_t b) {
    char buf[64];
    format_hex(buf, prefix, val);
    draw_string_small(x, y, buf, r, g, b);
}

#ifdef LILAC_UFS_SNAPSHOT
/* Sony live.dtb: /soc/ufshc@1da4000, reg = <0x01da4000 0x2500>.
 * This is a terminal, pre-XNU diagnostic. No queue, clock, reset, interrupt,
 * UIC or storage commands are written. Never dereference the queue addresses.
 * UIC error registers 0x38..0x48 can clear on read and are deliberately absent.
 * Skip command/list-clear registers and vendor registers as well.
 */
#define LILAC_UFS_BASE 0x01da4000ULL
static const struct {
    const char *label;
    uint32_t offset;
} ufs_snapshot_regs[] = {
    {"CAP     +000", 0x00},
    {"VER     +008", 0x08},
    {"HCS     +030", 0x30},
    {"HCE     +034", 0x34},
    {"HCDDID  +010", 0x10},
    {"HCPMID  +014", 0x14},
    {"AHIT    +018", 0x18},
    {"IS      +020", 0x20},
    {"IE      +024", 0x24},
    {"UTRLBA  +050", 0x50},
    {"UTRLBAU +054", 0x54},
    {"UTRLDBR +058", 0x58},
    {"UTRLRSR +060", 0x60},
    {"UTMRLBA +070", 0x70},
    {"UTMRLBAU+074", 0x74},
    {"UTMRLDBR+078", 0x78},
    {"UTMRLRSR+080", 0x80},
};
static volatile uint32_t ufs_snapshot_pending = 0xffffffffU;
extern uint32_t shim_ufs_read32(uint64_t address);

__attribute__((noreturn))
static void ufs_snapshot_halt(uint64_t initial_el, uint64_t mpidr) {
    fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 16, 20, 28);
    draw_string(24, 24, "LILAC UFS READ-ONLY SNAPSHOT v1", 100, 210, 255);
    draw_hex(24, 68, "Base: ", LILAC_UFS_BASE, 255, 220, 100);
    draw_hex_small(24, 112, "EL/Core: ", (initial_el << 32) | (mpidr & 0xffffffULL), 220, 220, 220);
    draw_string_small(24, 142, "One pass after ABL handoff; XNU is not entered.", 220, 220, 220);
    draw_string_small(24, 166, "Registers only. No UFS commands or MMIO writes.", 220, 220, 220);
    uint32_t cap = 0;
    for (size_t i = 0; i < sizeof(ufs_snapshot_regs) / sizeof(ufs_snapshot_regs[0]); i++) {
        int y = 218 + (int)i * 38;
        draw_string(24, y, ufs_snapshot_regs[i].label, 220, 220, 220);
        draw_string(280, y, "reading...", 255, 220, 100);
        ufs_snapshot_pending = ufs_snapshot_regs[i].offset;
        /* Make the last attempted access visible before a possible bus stall. */
        __asm__ volatile ("dsb sy" ::: "memory");
        uint32_t value = shim_ufs_read32(LILAC_UFS_BASE + ufs_snapshot_regs[i].offset);
        fill_rect(280, y, 710, y + 32, 16, 20, 28);
        draw_hex(280, y, "", value, 100, 255, 140);
        if (i == 0) cap = value;
        if (i == 1 && (cap == 0 || cap == 0xffffffffU || value == 0 || value == 0xffffffffU)) {
            draw_string(24, 916, "CAP/VER invalid; reads stopped.", 255, 110, 90);
            goto halt;
        }
    }
    ufs_snapshot_pending = 0xffffffffU;
    draw_string(24, 916, "SNAPSHOT COMPLETE - halted", 100, 255, 140);
halt:
    draw_string_small(24, 962, "Values are sequential samples, not an atomic snapshot.", 220, 220, 220);
    draw_string_small(24, 986, "This does not test disk reads, DMA or SMMU mappings.", 220, 220, 220);
    draw_string_small(24, 1010, "Photograph this panel, then return to Fastboot manually.", 220, 220, 220);
    __asm__ volatile ("dsb sy" ::: "memory");
    for (;;) __asm__ volatile ("wfi");
}
#endif

#ifdef LILAC_UFS_READ
static volatile uint32_t ufs_read_stage, ufs_read_detail;
void lilac_ufs_read_progress(uint32_t stage, uint32_t detail) {
    ufs_read_stage=stage; ufs_read_detail=detail;
    fill_rect(0, 94, FB_WIDTH, 142, 16, 20, 28);
    draw_hex_small(24, 98, "Stage: ", stage, 255, 220, 100);
    draw_hex_small(360, 98, "Detail: ", detail, 255, 220, 100);
}
static void ufs_hex32(int x, int y, uint32_t value) {
    char buf[9]; static const char hex[]="0123456789abcdef";
    for (int i=0;i<8;i++) buf[i]=hex[(value>>(28-i*4))&15];
    buf[8]=0; draw_string_small(x,y,buf,100,255,140);
}
static void ufs_read_value(int column, int row, const char *label, uint32_t value) {
    int x=24+column*340, y=154+row*24;
    draw_string_small(x,y,label,220,220,220);
    ufs_hex32(x+160,y,value);
}
__attribute__((noreturn))
static void ufs_read_halt(void) {
    struct ur_result r;
    fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 16, 20, 28);
    draw_string(24, 24, "LILAC UFS READ BATCH v2", 100, 210, 255);
    draw_string_small(24, 68, "LUN0: full GPT + repeated partition boundaries; read only", 220, 220, 220);
    lilac_ufs_read_probe(&r);
    ufs_read_value(0,0,"Error",r.error); ufs_read_value(1,0,"Completed",r.completed);
    ufs_read_value(0,1,"RAM restored",r.restored); ufs_read_value(1,1,"Checks",r.checks);
    ufs_read_value(0,2,"TR base",r.tr_base); ufs_read_value(1,2,"TM buffer",r.tm_base);
    ufs_read_value(0,3,"Old DW0",r.old_dw0); ufs_read_value(1,3,"Old CDB",r.old_cdb);
    ufs_read_value(0,4,"HCS",r.hcs); ufs_read_value(1,4,"IS after",r.is_after);
    ufs_read_value(0,5,"Submitted",r.submitted); ufs_read_value(1,5,"Elapsed ms",r.elapsed_ms);
    ufs_read_value(0,6,"Last LBA",r.last_lba); ufs_read_value(1,6,"Block bytes",r.block_bytes);
    ufs_read_value(0,7,"Header CRC",r.header_crc); ufs_read_value(1,7,"MBR CRC",r.mbr_crc);
    ufs_read_value(0,8,"Primary CRC",r.primary_crc); ufs_read_value(1,8,"Backup CRC",r.backup_crc);
    ufs_read_value(0,9,"Expected array",r.entries_crc); ufs_read_value(1,9,"Primary array",r.primary_entries_crc);
    ufs_read_value(0,10,"Backup array",r.backup_entries_crc); ufs_read_value(1,10,"Repeated array",r.repeat_entries_crc);
    ufs_read_value(0,11,"Entry count",r.entries_count); ufs_read_value(1,11,"Entry bytes",r.entry_bytes);
    ufs_read_value(0,12,"Partitions",r.partitions); ufs_read_value(1,12,"Sampled",r.sampled);
    ufs_read_value(0,13,"Last cmd LBA",r.failed_lba); ufs_read_value(1,13,"Doorbell",r.doorbell);
    ufs_read_value(0,14,"OCS",r.ocs); ufs_read_value(1,14,"Response DW0",r.response);
    ufs_read_value(0,15,"Data DW0",r.data_words[0]); ufs_read_value(1,15,"Data DW1",r.data_words[1]);
    ufs_read_value(0,16,"First usable",(uint32_t)r.first_usable); ufs_read_value(1,16,"Last usable",(uint32_t)r.last_usable);
    ufs_read_value(0,17,"TR run at end",r.tr_run); ufs_read_value(1,17,"Data DW2",r.data_words[2]);
    draw_string_small(24,604,"PARTITIONS: first 20 shown; see Checks/Sampled for validation",255,220,100);
    draw_string_small(24,632,"Name            First    Last     Head CRC Tail CRC Type",220,220,220);
    for (uint32_t i=0;i<r.partitions && i<20;i++) {
        char name[16]; for (uint32_t j=0;j<15;j++) name[j]=r.partition[i].name[j]; name[15]=0;
        int y=660+(int)i*22;
        draw_string_small(24,y,name,220,220,220);
        ufs_hex32(170,y,(uint32_t)r.partition[i].first); ufs_hex32(260,y,(uint32_t)r.partition[i].last);
        ufs_hex32(350,y,r.partition[i].head_crc); ufs_hex32(440,y,r.partition[i].tail_crc);
        static const char *types[]={"raw","boot","APFS","ext4","sparse","AVB","DTB"};
        draw_string_small(530,y,types[r.partition[i].signature],100,210,255);
    }
    draw_string(24,1120,r.error?"READ BATCH STOPPED":"READ + FULL GPT + SAMPLES PASS",r.error?255:100,r.error?110:255,140);
    draw_string_small(24,1166,"Checks: 01 capacity 02 MBR 04 headers 08 arrays 10 samples 20 final",220,220,220);
    draw_string_small(24,1190,"Hex values. Fingerprints do not validate filesystems. No storage writes.",220,220,220);
    draw_string_small(24,1214,"One final photo; use hardware restart to Fastboot afterward.",220,220,220);
    __asm__ volatile ("dsb sy" ::: "memory");
    for (;;) __asm__ volatile ("wfi");
}
#endif

#ifdef LILAC_UFS_HANDOFF
static volatile uint32_t ufs_handoff_stage, ufs_handoff_address;
static void handoff_value(int column, int y, const char *label, uint32_t value) {
    char buf[11];
    static const char hex[]="0123456789abcdef";
    buf[0]='0'; buf[1]='x';
    for (int i=0;i<8;i++) buf[i+2]=hex[(value>>(28-i*4))&15];
    buf[10]=0;
    int x=24+column*340;
    draw_string_small(x,y,label,220,220,220);
    draw_string_small(x+140,y,buf,100,255,140);
}
void lilac_ufs_handoff_progress(uint32_t stage, uint32_t address) {
    ufs_handoff_stage=stage; ufs_handoff_address=address;
    /* Per-word metadata for fault reporting; update the framebuffer only at
     * each stage or 256-byte boundary to keep the observation interval short. */
    if (!(address&255) || stage==7) {
        fill_rect(0,94,FB_WIDTH,120,16,20,28);
        handoff_value(0,98,"Stage",stage);
        handoff_value(1,98,"Address",address);
    }
}
__attribute__((noreturn))
static void ufs_handoff_halt(void) {
    struct uh_result r;
    static const char *labels[UH_REGISTER_COUNT]={
        "CAP","VER","HCS","HCE","IS","IE","TR base","TR upper",
        "TM base","TM upper","TR doorbell","TR run","TM doorbell","TM run"
    };
    static const char *words[8]={"DW0","DW1","DW2","DW3","DW4","DW5","DW6","DW7"};
    fill_rect(0,0,FB_WIDTH,FB_HEIGHT,16,20,28);
    draw_string(24,24,"LILAC UFS HANDOFF v1",100,210,255);
    draw_string_small(24,68,"Post-ABL queue observation; halt before XNU",220,220,220);
    lilac_ufs_handoff_snapshot(&r);
    handoff_value(0,134,"Error",r.error); handoff_value(1,134,"Stable",r.stable);
    for (int i=0;i<UH_REGISTER_COUNT;i++) handoff_value(i%2,170+(i/2)*24,labels[i],r.reg[i]);
    handoff_value(0,338,"TR CRC32",r.tr_crc); handoff_value(1,338,"TM CRC32",r.tm_crc);
    draw_string_small(24,382,"UTRD (raw LE words)",255,220,100);
    draw_string_small(364,382,"Request (raw LE words)",255,220,100);
    for (int i=0;i<8;i++) {
        handoff_value(0,410+i*24,words[i],r.descriptor[i]);
        handoff_value(1,410+i*24,words[i],r.request[i]);
    }
    draw_string_small(24,622,"Response (raw LE words)",255,220,100);
    draw_string_small(364,622,"PRDT (raw LE words)",255,220,100);
    for (int i=0;i<8;i++) {
        handoff_value(0,650+i*24,words[i],r.response[i]);
        if (i<4) handoff_value(1,650+i*24,words[i],r.prdt[i]);
    }
    handoff_value(0,868,"Direction",r.direction); handoff_value(1,868,"Opcode",r.opcode);
    handoff_value(0,892,"LUN",r.lun); handoff_value(1,892,"Tag",r.tag);
    handoff_value(0,916,"LBA (10)",r.lba); handoff_value(1,916,"Blocks (10)",r.blocks);
    handoff_value(0,940,"Bytes",r.bytes); handoff_value(1,940,"SCSI status",r.scsi_status);
    handoff_value(0,964,"Read guard",r.read_guard); handoff_value(1,964,"Captured",r.captured);
    draw_string(24,1010,r.error?"HANDOFF CAPTURE STOPPED":"HANDOFF CAPTURE COMPLETE",r.error?255:100,r.error?110:255,140);
    draw_string_small(24,1060,"No probe commands, MMIO stores or firmware RAM writes.",220,220,220);
    draw_string_small(24,1084,"Old PRD data address is never dereferenced.",220,220,220);
    draw_string_small(24,1108,"Stable samples do not prove DMA mapping or disk reads.",220,220,220);
    draw_string_small(24,1132,"Photograph entire panel; restart to Fastboot manually.",220,220,220);
    __asm__ volatile ("dsb sy" ::: "memory");
    for (;;) __asm__ volatile ("wfi");
}
#endif

/* ------------------------------------------------------------------------ */
/* Apple Device Tree Writer                                                 */
/* ------------------------------------------------------------------------ */

struct dt_writer {
    uint8_t *base, *cur;
    uint8_t *end;
    int overflow;
    uint32_t scratch[2];
    uint32_t *counts[16];
    int depth;
};

static void dt_prop(struct dt_writer *w, const char *name, const void *data, uint32_t len) {
    uint64_t padded = ((uint64_t)len + 3ULL) & ~3ULL;
    if (w->overflow || str_len(name) > 32 || (uint64_t)(w->end - w->cur) < 36ULL + padded) {
        w->overflow = 1;
        return;
    }
    shim_memset(w->cur, 0, 32);
    shim_memcpy(w->cur, name, str_len(name));
    *(uint32_t *)(w->cur + 32) = len;
    shim_memset(w->cur + 36, 0, (size_t)padded);
    shim_memcpy(w->cur + 36, data, len);
    w->cur += 36ULL + padded;
    w->counts[w->depth - 1][0]++;
}

static void dt_prop_str(struct dt_writer *w, const char *name, const char *value) {
    dt_prop(w, name, value, (uint32_t)str_len(value) + 1);
}

static void dt_prop_u32(struct dt_writer *w, const char *name, uint32_t value) {
    dt_prop(w, name, &value, sizeof(value));
}

static void dt_prop_u64(struct dt_writer *w, const char *name, uint64_t value) {
    dt_prop(w, name, &value, sizeof(value));
}

static void dt_prop_padded(struct dt_writer *w, const char *name, const char *value, uint32_t size) {
    uint8_t field[64];
    shim_memset(field, 0, sizeof(field));
    shim_memcpy(field, value, str_len(value));
    dt_prop(w, name, field, size);
}

static void dt_begin(struct dt_writer *w, const char *name) {
    if (w->depth > 0) {
        w->counts[w->depth - 1][1]++;
    }
    if (w->overflow || (uint64_t)(w->end - w->cur) < 8) {
        w->overflow = 1;
        w->counts[w->depth++] = w->scratch;
        return;
    }
    w->counts[w->depth++] = (uint32_t *)w->cur;
    shim_memset(w->cur, 0, 8);
    w->cur += 8;
    dt_prop_str(w, "name", name);
}

static void dt_end(struct dt_writer *w) {
    w->depth--;
}

static void nvram_chrp_header(uint8_t *h, uint8_t sig, uint32_t bytes, const char *name) {
    uint16_t blocks = (uint16_t)(bytes / 16), sum;
    shim_memset(h, 0, 16);
    h[0] = sig;
    h[2] = (uint8_t)blocks;
    h[3] = (uint8_t)(blocks >> 8);
    shim_memcpy(h + 4, name, str_len(name));
    sum = h[0];
    for (int i = 2; i < 16; i++) sum += h[i];
    while (sum > 0xff) sum = (sum & 0xff) + (sum >> 8);
    h[1] = (uint8_t)sum;
}

static void build_nvram_image(uint8_t *image) {
    uint32_t lo = 1, hi = 0, generation = 1;
    shim_memset(image, 0, NVRAM_BANK_SIZE);
    nvram_chrp_header(image, 0x5a, 0x20, "nvram");
    nvram_chrp_header(image + 0x20, 0x70, NVRAM_BANK_SIZE - 0x20, "common");
    shim_memcpy(image + 20, &generation, 4);
    for (uint32_t i = 20; i < NVRAM_BANK_SIZE; i++) {
        lo = (lo + image[i]) % 65521;
        hi = (hi + lo) % 65521;
    }
    uint32_t adler = (hi << 16) | lo;
    shim_memcpy(image + 16, &adler, 4);
}

/* ------------------------------------------------------------------------ */
/* Return from Kernel / Trap Handler                                        */
/* ------------------------------------------------------------------------ */

static int render_log_slice(const char *log, size_t from, size_t to, int y, int max_y,
                            uint8_t r, uint8_t g, uint8_t b) {
    int x = 24;
    for (size_t i = from; i < to && y <= max_y; i++) {
        char c = log[i];
        if (c == '\r') continue;
        if (c == '\n') {
            y += 18;
            x = 24;
        } else {
            if (c < 32 || c > 126) c = '.';
            draw_char_small(x, y, c, r, g, b);
            x += 9;
            if (x > 690) {
                y += 18;
                x = 24;
            }
        }
    }
    if (x > 24) y += 18;
    return y;
}

static size_t log_find_last(const char *log, size_t length, const char *needle) {
    size_t n = str_len(needle), found = length;
    if (n > length) return found;
    for (size_t i = 0; i <= length - n; i++) {
        size_t j = 0;
        while (j < n && log[i + j] == needle[j]) j++;
        if (j == n) found = i;
    }
    return found;
}

#ifdef LILAC_USB_DEBUG
void lilac_debug_display(const char *message, uint32_t value) {
    fill_rect(0, 1170, FB_WIDTH, FB_HEIGHT, 16, 20, 28);
    draw_string_small(24, 1180, message, 100, 255, 140);
    draw_hex_small(24, 1204, "USB detail: ", value, 255, 220, 100);
    draw_hex_small(24, 1228, "MMIO FAR: ", shim_debug_fault_far, 255, 220, 100);
}

void lilac_debug_diagnostics(uint32_t stage, uint32_t address, uint32_t value,
    uint32_t event_count, uint32_t last_event) {
    fill_rect(0, 1040, FB_WIDTH, 1170, 16, 20, 28);
    draw_hex_small(24, 1048, "USB stage: ", stage, 255, 220, 100);
    draw_hex_small(24, 1070, "Register: ", address, 255, 220, 100);
    draw_hex_small(24, 1092, "Value: ", value, 255, 220, 100);
    draw_hex_small(24, 1114, "Events: ", event_count, 255, 220, 100);
    draw_hex_small(24, 1136, "Last event: ", last_event, 255, 220, 100);
}
#endif

#ifdef LILAC_SMP
static int shim_check_smp_return(uint64_t reason, uint64_t esr, uint64_t elr,
                                 uint64_t far, uint64_t lr, uint64_t sp) {
    /* No topology has been published yet on an early BootShim exception. */
    if (lent_count == 0) return 1;
    uint64_t current;
    int64_t states[LILAC_SMP_MAX_CPUS] = {0};
    __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(current));
    enum lilac_smp_return_result result = lilac_smp_check_return(
        lent_mpidrs, lent_count, (uint32_t)current, shim_affinity, NULL, states);
    if (result == LILAC_SMP_RETURN_OK) return 1;

    /* Preserve the XNU panel below y=560. No USB or DMA is started here. */
    fill_rect(0, 0, FB_WIDTH, 550, 16, 20, 28);
    draw_string_small(24, 16, "SMP return refused - USB remains off", 255, 90, 90);
    const char *detail = result == LILAC_SMP_RETURN_BAD_TOPOLOGY ? "Invalid saved CPU topology" :
        result == LILAC_SMP_RETURN_WRONG_CPU ? "Return is not on the boot CPU" :
        "Firmware did not confirm every borrowed CPU OFF";
    draw_string_small(24, 40, detail, 255, 190, 120);
    draw_hex_small(24, 68, "Reason: ", reason, 255, 220, 100);
    draw_hex_small(360, 68, "Count: ", lent_count, 255, 220, 100);
    draw_hex_small(24, 92, "Current: ", current, 160, 210, 255);
    draw_hex_small(360, 92, "Boot: ", lent_mpidrs[0], 160, 210, 255);
    draw_string_small(24, 120, "PSCI: 0=ON  1=OFF  2=PENDING  others=error", 240, 240, 245);
    if (result != LILAC_SMP_RETURN_BAD_TOPOLOGY) {
        for (unsigned i = 1; i < lent_count; i++) {
            int y = 146 + (int)(i - 1) * 24;
            draw_hex_small(24, y, "MPIDR: ", lent_mpidrs[i], 160, 210, 255);
            draw_hex_small(360, y, "State: ", (uint64_t)states[i],
                           255, states[i] == 1 ? 220 : 90, 100);
        }
    }
    if (reason == 1 || reason == 0xdead) {
        draw_hex_small(24, 334, "ESR: ", esr, 255, 190, 120);
        draw_hex_small(360, 334, "ELR: ", elr, 255, 190, 120);
        draw_hex_small(24, 358, "FAR: ", far, 255, 190, 120);
        draw_hex_small(360, 358, "LR: ", lr, 255, 190, 120);
        draw_hex_small(24, 382, "SP: ", sp, 255, 190, 120);
    }
    __asm__ volatile ("dsb sy" ::: "memory");
    return 0;
}
#endif

void shim_on_kernel_return(uint64_t log_pa, uint64_t log_len, uint64_t reason,
                           uint64_t esr, uint64_t elr, uint64_t far, uint64_t lr, uint64_t sp) {
    (void)sp;
#ifdef LILAC_SMP
    /* Independent final guard: USB/DMA can reuse RAM only after every core
     * lent to XNU has actually powered off, including early-return paths. */
    if (!shim_check_smp_return(reason, esr, elr, far, lr, sp)) return;
#endif
#ifdef LILAC_UFS_HANDOFF
    if (reason == 0xdead) {
        fill_rect(0,1000,FB_WIDTH,FB_HEIGHT,16,20,28);
        draw_string(24,1010,"UFS HANDOFF FAULT - halted",255,110,90);
        handoff_value(0,1060,"Stage",ufs_handoff_stage);
        handoff_value(1,1060,"Address",ufs_handoff_address);
        draw_hex_small(24,1092,"ESR: ",esr,160,210,255);
        draw_hex_small(24,1116,"ELR: ",elr,160,210,255);
        draw_hex_small(24,1140,"FAR: ",far,160,210,255);
        __asm__ volatile ("dsb sy" ::: "memory");
        return;
    }
#endif
#ifdef LILAC_UFS_READ
    if (reason == 0xdead) {
        fill_rect(0, 930, FB_WIDTH, FB_HEIGHT, 16, 20, 28);
        draw_string(24, 946, "UFS READ FAULT - halted", 255, 110, 90);
        draw_hex_small(24, 992, "Stage: ", ufs_read_stage, 255, 220, 100);
        draw_hex_small(360, 992, "Detail: ", ufs_read_detail, 255, 220, 100);
        draw_hex_small(24, 1020, "ESR: ", esr, 160, 210, 255);
        draw_hex_small(24, 1048, "ELR: ", elr, 160, 210, 255);
        draw_hex_small(24, 1076, "FAR: ", far, 160, 210, 255);
        __asm__ volatile ("dsb sy" ::: "memory");
        return;
    }
#endif
#ifdef LILAC_UFS_SNAPSHOT
    if (reason == 0xdead) {
        /* Preserve completed rows and the last attempted register on a fault. */
        fill_rect(0, 900, FB_WIDTH, FB_HEIGHT, 16, 20, 28);
        draw_string(24, 916, "SNAPSHOT FAULT - halted", 255, 110, 90);
        draw_hex_small(24, 964, "Pending offset: ", ufs_snapshot_pending, 255, 220, 100);
        draw_hex_small(24, 990, "ESR: ", esr, 160, 210, 255);
        draw_hex_small(24, 1016, "ELR: ", elr, 160, 210, 255);
        draw_hex_small(24, 1042, "FAR: ", far, 160, 210, 255);
        __asm__ volatile ("dsb sy" ::: "memory");
        return;
    }
#endif
    fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 16, 20, 28);

    if (reason == 0xdead) {
        draw_string_small(24, 16, ">>> BOOTSHIM INTERNAL EXCEPTION <<<", 255, 90, 90);
    } else if (reason == 1) {
        draw_string_small(24, 16, ">>> XNU EARLY EXCEPTION RETURN <<<", 255, 110, 90);
    } else {
        draw_string_small(24, 16, ">>> XNU RETURNED TO BOOTSHIM <<<", 100, 255, 140);
    }

    draw_hex_small(24,  38, "Reason: ", reason,  255, 220, 100);
    draw_hex_small(360, 38, "LogLen: ", log_len, 255, 220, 100);
    int y = 64;
    /* Ordinary loader_return passes only x0..x3; x4..x7 are not trap registers. */
    if (reason == 1 || reason == 0xdead) {
        draw_hex_small(24,  y, "ESR: ", esr, 160, 210, 255);
        draw_hex_small(360, y, "ELR: ", elr, 160, 210, 255);
        y += 20;
        draw_hex_small(24,  y, "FAR: ", far, 160, 210, 255);
        draw_hex_small(360, y, "LR:  ", lr,  160, 210, 255);
        y += 24;
    }

    if (log_len > 0 && log_pa >= 0x80000000ULL && log_pa < 0xf8000000ULL &&
        log_len <= 0xf8000000ULL - log_pa) {
        if (log_len > 2 * 1024 * 1024) log_len = 2 * 1024 * 1024;
        const char *log = (const char *)log_pa;
        size_t panic_start = log_find_last(log, log_len, "\n--- panic text ---\n");
        if (panic_start < log_len) {
            y = render_log_slice(log, panic_start, log_len, y, y + 198, 255, 205, 135);
            y += 12;
        }
        /* The message buffer may already contain the long panic report. */
        size_t recent_end = log_find_last(log, panic_start, "panic(cpu ");
        size_t tail_start = recent_end;
        int tail_lines = 0;
        while (tail_start > 0 && tail_lines < 42) {
            tail_start--;
            if (log[tail_start] == '\n') tail_lines++;
        }
        if (tail_start > 0 && log[tail_start] == '\n') tail_start++;

        draw_string_small(24, y, "--- recent kernel / userspace output ---", 100, 210, 255);
        render_log_slice(log, tail_start, recent_end, y + 22, 1250, 240, 240, 245);
    } else {
        draw_string_small(24, y, "(No kernel log text captured yet)", 200, 200, 200);
    }
#ifdef LILAC_USB_DEBUG
    if (log_len && log_len <= 2 * 1024 * 1024 && log_pa >= 0x80000000ULL &&
        log_pa < 0xf8000000ULL && log_len <= 0xf8000000ULL - log_pa)
        lilac_debug_usb((const void *)log_pa, (uint32_t)log_len);
#endif
}

static void jump_to_xnu(uint64_t entry_pa, uint64_t boot_args_pa) {
    __asm__ volatile (
        "msr daifset, #0xf\n"
        "ic iallu\n"
        "dsb sy\n"
        "isb\n"
        "mov x0, %0\n"
        "br %1\n"
        :
        : "r"(boot_args_pa), "r"(entry_pa)
        : "x0", "x1", "x2", "x3", "memory"
    );
}

void shim_main(const uint8_t *payload_ptr, uint64_t dtb_ptr, uint64_t initial_el) {
    (void)dtb_ptr;
    (void)initial_el;
#ifdef LILAC_USB_SELF_TEST
    fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 16, 20, 28);
    static const char usb_test[] = "LILAC USB SELF TEST\nRAM only; no XNU, no persistent storage writes.\n";
    lilac_debug_usb(usb_test, sizeof(usb_test) - 1);
    return;
#endif
    uint64_t mpidr;
    __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(mpidr));

#ifdef LILAC_UFS_SNAPSHOT
    ufs_snapshot_halt(initial_el, mpidr);
#endif
#ifdef LILAC_UFS_READ
    ufs_read_halt();
#endif
#ifdef LILAC_UFS_HANDOFF
    ufs_handoff_halt();
#endif
#ifndef LILAC_QUIET_BOOT
    draw_string(40, 40, "SONY XZ1 COMPACT - XNU LOADER", 210, 0, 0);
    draw_hex(40, 76,  "Payload: ", (uint64_t)payload_ptr, 0, 60, 180);
    draw_hex(40, 112, "EL/Core: ", (initial_el << 32) | (mpidr & 0xffffffULL), 0, 60, 180);
#endif

    const struct lilac_payload *pl = (const struct lilac_payload *)payload_ptr;
    const uint8_t *macho_ptr = payload_ptr;
    const uint8_t *rd_src = NULL;
    uint32_t rd_sz = 0;

    if (pl->magic == 0x4c494c43) {
        macho_ptr = payload_ptr + sizeof(*pl);
        uint32_t k_aligned = (pl->kernel_sz + 4095) & ~4095;
        rd_src = payload_ptr + sizeof(*pl) + k_aligned;
        rd_sz = pl->rd_sz;
    }

    const struct mach_header_64 *mh = (const struct mach_header_64 *)macho_ptr;
    if (mh->magic != 0xfeedfacf) {
        draw_hex(40, 148, "BadMagic:", mh->magic, 220, 0, 0);
        return;
    }

    uint64_t slide = PHYS_BASE - KERNEL_VIRT_BASE;
    uint64_t entry_va = 0;
    uint64_t vm_lo = ~0ULL, vm_hi = 0;

    const struct load_command *lc = (const struct load_command *)(macho_ptr + sizeof(*mh));
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (seg->vmsize > 0) {
                if (seg->vmaddr < vm_lo) vm_lo = seg->vmaddr;
                if (seg->vmaddr + seg->vmsize > vm_hi) vm_hi = seg->vmaddr + seg->vmsize;

                uint64_t dst_pa = seg->vmaddr + slide;
                shim_memset((void *)dst_pa, 0, seg->vmsize);
                if (seg->filesize > 0) {
                    shim_memcpy((void *)dst_pa, macho_ptr + seg->fileoff, seg->filesize);
                }
            }
        } else if (lc->cmd == LC_UNIXTHREAD) {
            entry_va = ((const uint64_t *)((const uint8_t *)lc + 16))[32];
        }
        lc = (const struct load_command *)((const uint8_t *)lc + lc->cmdsize);
    }

    if (entry_va == 0 || vm_lo == ~0ULL) {
        draw_string(40, 148, "ERROR: Entry Point Not Found!", 220, 0, 0);
        return;
    }

    uint64_t kern_lo = vm_lo + slide;
    uint64_t kern_hi = (vm_hi + slide + 0x3fffULL) & ~0x3fffULL;

    /* mockfs populates whole 16KiB VM pages; memdev's 4KiB units are only an ABI. */
    uint64_t rd_pa = (kern_hi + 0x10000ULL);
    uint64_t rd_sz_page = ((uint64_t)rd_sz + 16383u) & ~16383ULL;
    if (rd_src != NULL && rd_sz > 0) {
        shim_memset((void *)rd_pa, 0, rd_sz_page);
        shim_memcpy((void *)rd_pa, rd_src, rd_sz);
        flush_dcache(rd_pa, rd_pa + rd_sz_page);
    }

    /* Hook XNULDRST return stub */
    int hooked_stub = 0;
    for (uint64_t *q = (uint64_t *)kern_lo; q < (uint64_t *)kern_hi; q++) {
        if (*q == 0x545352444C554E58ULL /* "XNULDRST" */) {
            q[1] = (uint64_t)(uintptr_t)&shim_return_stub;
            q[2] = 0; /* 0 = run until panic or loader_return_after timer */
            q[3] = slide;
            hooked_stub = 1;
            break;
        }
    }

#ifndef LILAC_QUIET_BOOT
    draw_string(40, 148, hooked_stub ? "Unpacked + XNULDRST Hook OK" : "Unpacked (No XNULDRST)", 0, 140, 0);
#else
    (void)hooked_stub;
#endif

    /* -------------------------------------------------------------------- */
    /* Build Apple Device Tree                                              */
    /* -------------------------------------------------------------------- */
    uint64_t dt_pa = (rd_src != NULL && rd_sz > 0) ?
        ((rd_pa + rd_sz_page + 0x3fffULL) & ~0x3fffULL) : (kern_hi + 0x10000ULL);
    uint32_t dt_max = 0x80000u; /* 512 KB */
    struct dt_writer w;
    shim_memset(&w, 0, sizeof(w));
    w.base = (uint8_t *)dt_pa;
    w.cur  = (uint8_t *)dt_pa;
    w.end  = (uint8_t *)(dt_pa + dt_max);

    uint64_t cntfrq;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    uint8_t seed[64];
    uint64_t vct;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(vct));
    for (unsigned i = 0; i < sizeof(seed); i++) {
        seed[i] = (uint8_t)(vct >> (i & 7)) ^ (uint8_t)(i * 37);
    }

    dt_begin(&w, "device-tree");
    dt_prop_str(&w, "model", "iPhone10,3");
    dt_prop_str(&w, "target-type", "D22AP");
    dt_prop_str(&w, "compatible", "iPhone10,3");
    dt_prop_str(&w, "platform-name", "t8015");
    dt_prop_padded(&w, "serial-number", "LILAC0000001", 32);
    dt_prop_padded(&w, "mlb-serial-number", "LILAC0000001MLB00", 32);
    dt_prop_padded(&w, "model-number", "MQAC2", 32);
    dt_prop_padded(&w, "region-info", "LL/A", 32);
    dt_prop_padded(&w, "regulatory-model-number", "A1865", 32);
    dt_prop_u32(&w, "#address-cells", 2);
    dt_prop_u32(&w, "#size-cells", 2);
    dt_prop_u64(&w, "clock-frequency", cntfrq);

    dt_begin(&w, "chosen");
    dt_prop(&w, "random-seed", seed, sizeof(seed));
    dt_prop_u64(&w, "dram-base", PHYS_BASE);
    dt_prop_u64(&w, "dram-size", MEM_SIZE);
    dt_prop_u32(&w, "debug-enabled", 1);
    dt_prop_u32(&w, "display-scale", 3);
    dt_prop_u32(&w, "display-rotation", 0);
    dt_prop_str(&w, "psci-method", "smc");
    dt_prop_u64(&w, "boot-epoch", 1762000000ULL);
    static const uint8_t wifi_mac[6] = { 0x02, 0x53, 0x4f, 0x4e, 0x59, 0x01 };
    static const uint8_t bt_mac[6]   = { 0x02, 0x53, 0x4f, 0x4e, 0x59, 0x02 };
    static const uint8_t manifest_hash[48] = { 0x6c, 0x69, 0x6c, 0x61, 0x63 };
    static const uint8_t no_behavior[16] = { 0 };
    dt_prop_u64(&w, "unique-chip-id", 0x000052445730001ULL);
    dt_prop_u32(&w, "chip-id", 0x8015);
    dt_prop_u32(&w, "board-id", 0x06);
    dt_prop(&w, "mac-address-wifi0", wifi_mac, sizeof(wifi_mac));
    dt_prop(&w, "mac-address-bluetooth0", bt_mac, sizeof(bt_mac));
    dt_prop(&w, "boot-manifest-hash", manifest_hash, sizeof(manifest_hash));
    dt_prop(&w, "software-behavior", no_behavior, sizeof(no_behavior));
    dt_prop_str(&w, "firmware-version", "iBoot-8422.100.650");
    dt_prop_u32(&w, "certificate-production-status", 1);
    dt_prop_u32(&w, "certificate-security-mode", 1);
    dt_prop_u32(&w, "effective-production-status-ap", 1);
    dt_prop_u32(&w, "effective-security-mode-ap", 1);
    dt_prop_u32(&w, "secure-boot", 1);
    dt_prop_u32(&w, "production-cert", 1);
    dt_prop_u32(&w, "development-cert", 0);
    dt_prop_u32(&w, "embedded-panic-log-size", PANIC_LOG_SIZE);

    static uint8_t nvram_image[NVRAM_BANK_SIZE] __attribute__((aligned(8)));
    build_nvram_image(nvram_image);
    dt_prop_u32(&w, "nvram-bank-size", NVRAM_BANK_SIZE);
    dt_prop_u32(&w, "nvram-bank-count", 1);
    dt_prop_u32(&w, "nvram-current-bank", 0);
    dt_prop(&w, "nvram-proxy-data", nvram_image, sizeof(nvram_image));

    dt_begin(&w, "memory-map");
    if (rd_src != NULL && rd_sz > 0) {
        uint64_t ramdisk_prop[2] = { rd_pa, (uint64_t)rd_sz_page };
        dt_prop(&w, "RAMDisk", ramdisk_prop, sizeof(ramdisk_prop));
    }
    dt_end(&w);
    dt_end(&w); /* chosen */

    dt_begin(&w, "defaults");
    dt_prop_u32(&w, "kern.vm_compressor", 2);
    dt_end(&w);

    dt_begin(&w, "product");
    dt_prop_str(&w, "product-name", "iPhone X");
    /* iPhone X product properties from the 20F66 D22AP device tree.
     * Without these, MobileGestalt can select a generic phone UI. */
    dt_prop_u32(&w, "thin-bezel", 1);
    dt_prop_u32(&w, "artwork-device-subtype", 0x984);
    dt_prop_u32(&w, "artwork-scale-factor", 3);
    dt_prop_str(&w, "artwork-device-idiom", "phone");
    dt_end(&w);

    dt_begin(&w, "options");
    dt_end(&w);

    shim_memset((void *)PANIC_LOG_BASE, 0, PANIC_LOG_SIZE);
    dt_begin(&w, "pram");
    uint64_t pram_reg[2] = { PANIC_LOG_BASE, PANIC_LOG_SIZE };
    dt_prop(&w, "reg", pram_reg, sizeof(pram_reg));
    dt_end(&w);

    dt_begin(&w, "cpus");
#ifdef LILAC_SMP
    uint32_t cpu_mpidrs[LILAC_SMP_MAX_CPUS];
    unsigned cpu_count=lilac_smp_topology((uint32_t)mpidr,shim_affinity,NULL,cpu_mpidrs);
    for(unsigned i=0;i<cpu_count;i++) lent_mpidrs[i]=cpu_mpidrs[i];
    lent_count=cpu_count;
#else
    uint32_t cpu_mpidrs[1]={(uint32_t)(mpidr&0xffffu)};
    unsigned cpu_count=1;
#endif
    for(unsigned i=0;i<cpu_count;i++) {
    char cpu_name[5]={'c','p','u',(char)('0'+i),0};
    dt_begin(&w, cpu_name);
    dt_prop_str(&w, "device_type", "cpu");
    dt_prop_str(&w, "state", i==0?"running":"waiting");
    dt_prop_u32(&w, "reg", cpu_mpidrs[i]);
    dt_prop_u32(&w, "cpu-id", i);
    dt_prop_u64(&w, "timebase-frequency", cntfrq);
    dt_prop_u64(&w, "fixed-frequency", cntfrq);
    dt_prop_u64(&w, "clock-frequency", 1900000000ULL);
    dt_prop_u64(&w, "bus-frequency", 100000000ULL);
    dt_prop_u64(&w, "memory-frequency", 100000000ULL);
    dt_prop_u64(&w, "peripheral-frequency", 24000000ULL);
    dt_end(&w);
    }
    dt_end(&w); /* cpus */

    dt_begin(&w, "arm-io");
    dt_prop_str(&w, "device_type", "msm8998");
    uint64_t ranges[3] = { 0, 0, 0x20000000ULL };
    dt_prop(&w, "ranges", ranges, sizeof(ranges));
    dt_prop_u32(&w, "#address-cells", 2);
    dt_prop_u32(&w, "#size-cells", 2);

    dt_begin(&w, "gic");
    dt_prop_str(&w, "device_type", "interrupt-controller");
    dt_prop_str(&w, "compatible", "arm,gic-v3");
    uint64_t gic_reg[4] = { 0x17a00000ULL, 0x10000ULL, 0x17b00000ULL, 0x100000ULL };
    dt_prop(&w, "reg", gic_reg, sizeof(gic_reg));
    /* Sony's /soc/timer virtual interrupt is <GIC_PPI 3 ...>: 16 + 3. */
    dt_prop_u32(&w, "virtual-timer-intid", 19);
    dt_end(&w);

    dt_end(&w); /* arm-io */
    dt_end(&w); /* device-tree */

    if (w.overflow) {
        draw_string(40, 184, "ERROR: Device Tree Overflow!", 220, 0, 0);
        return;
    }

    uint32_t dt_len = (uint32_t)(w.cur - w.base);
#ifndef LILAC_QUIET_BOOT
    draw_string(40, 184, (rd_src != NULL && rd_sz > 0) ?
        "Apple DT + RamDisk (md0) OK" : "Apple DeviceTree Built OK", 0, 140, 0);
#endif

    /* -------------------------------------------------------------------- */
    /* Set up boot_args                                                     */
    /* -------------------------------------------------------------------- */
    uint64_t ba_pa = (dt_pa + dt_len + 0x3fffULL) & ~0x3fffULL;
    boot_args *ba = (boot_args *)ba_pa;
    shim_memset(ba, 0, sizeof(*ba));

    uint64_t top = (ba_pa + sizeof(boot_args) + 0x3fffULL) & ~0x3fffULL;

    ba->Revision = kBootArgsRevision2;
    ba->Version = kBootArgsVersion2;
    ba->virtBase = KERNEL_VIRT_BASE;
    ba->physBase = PHYS_BASE;
    ba->memSize = MEM_SIZE;
    ba->memSizeActual = MEM_SIZE;
    ba->topOfKernelData = top;
    ba->deviceTreeP = (void *)(dt_pa - slide); /* Virtual address in XNU map */
    ba->deviceTreeLength = dt_len;

    ba->Video.v_baseAddr = FB_BASE;
#ifdef LILAC_QUIET_BOOT
    ba->Video.v_display = 1; /* graphics mode: no normal kernel text over scanout */
#else
    ba->Video.v_display = 0; /* live XNU text console for diagnostic boots */
#endif
    ba->Video.v_rowBytes = FB_WIDTH * 3;
    ba->Video.v_width = FB_WIDTH;
    ba->Video.v_height = FB_HEIGHT;
    ba->Video.v_depth = 24;

    const char *cmd = (rd_src != NULL && rd_sz > 0) ?
        LILAC_RAMDISK_BOOT_ARGS :
        "debug=0x8 serial=0 cpus=1 loader_return_after=5 -v";
    size_t cmd_len = 0;
    while (cmd[cmd_len] && cmd_len < sizeof(ba->CommandLine) - 1) {
        ba->CommandLine[cmd_len] = cmd[cmd_len];
        cmd_len++;
    }
    ba->CommandLine[cmd_len] = '\0';

    uint64_t entry_pa = entry_va + slide;
#ifndef LILAC_QUIET_BOOT
    draw_hex(40, 220, "Jump XNU: ", entry_pa, 150, 0, 160);
#endif

    /* Clean entire loaded kernel + metadata + ramdisk + shim stub + FB from D-cache into RAM */
    flush_dcache(FB_BASE, FB_BASE + (uint64_t)(FB_WIDTH * FB_HEIGHT * 3));
    flush_dcache(0x80080000ULL, 0x80098000ULL);
    flush_dcache(PHYS_BASE, top);

    jump_to_xnu(entry_pa, ba_pa);
}
