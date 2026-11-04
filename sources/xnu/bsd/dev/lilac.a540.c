/* SPDX-License-Identifier: GPL-2.0-only
 * MSM8998/A540 bring-up while the caller owns the CX vote. PMIC and clock
 * ordering follows Sony 696bf916 clock-gpu-8998.c and Linux v6.12 gdsc.c.
 * Every wait is bounded. GX must be collapsed before the rail is released.
 */
#include <mach/mach_types.h>
#include <kern/clock.h>
#include <arm/thread.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <libkern/libkern.h>
#include <mach/vm_param.h>
#include <vm/vm_kern.h>
#include "../../../../kernel/lilac-pm8005.h"
#include "../../../../kernel/lilac-a540-power.h"
#include "../../../../kernel/lilac-rpm-api.h"
#include "../../../../kernel/lilac-a540-me.h"
#include "../../../../kernel/lilac-a540-zap.h"
#include "../../../../kernel/lilac-a5xx-submit.h"
#include "../../../../kernel/lilac-gpu-va.h"
#include <mach/mach_time.h>
#include <sys/hakios.input.h>
#if defined(HAKIOS_ADRENO)
#error A540 and A642L backends cannot share one GPU service
#endif

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern int lilac_a540_load32(vm_offset_t, uint32_t *);
extern int lilac_a540_store32(vm_offset_t, uint32_t);
extern int lilac_a540_store64(vm_offset_t, uint64_t);
extern void lilac_a540_store64_pc(void);
extern void lilac_a540_load_pc(void), lilac_a540_store_pc(void), lilac_a540_recover_pc(void);
int lilac_a540_probe(void);
bool lilac_a540_abort(arm_saved_state_t *, uint32_t, vm_offset_t);
__asm__(".text\n"
    ".globl _lilac_a540_load32, _lilac_a540_load_pc, _lilac_a540_store32, _lilac_a540_store_pc, _lilac_a540_recover_pc\n"
    "_lilac_a540_load32:\n_lilac_a540_load_pc: ldr w2, [x0]\ndsb sy\nstr w2, [x1]\nmov w0, #1\nret\n"
    "_lilac_a540_store32:\n_lilac_a540_store_pc: str w1, [x0]\ndsb sy\nmov w0, #1\nret\n"
    ".globl _lilac_a540_store64, _lilac_a540_store64_pc\n"
    "_lilac_a540_store64:\n_lilac_a540_store64_pc: str x1, [x0]\ndsb sy\nmov w0, #1\nret\n"
    "_lilac_a540_recover_pc: mov w0, #0\nret\n");

static struct { uint64_t physical, size; vm_offset_t virtual; } maps[64];
static uint32_t map_count, fault_esr;
static vm_offset_t fault_address;
static bool probing;
static vm_offset_t address_of(uint64_t physical)
{
    for (uint32_t i = 0; i < map_count; i++)
        if (physical >= maps[i].physical && physical - maps[i].physical < maps[i].size)
            return maps[i].virtual + physical - maps[i].physical;
    if (map_count == 64) return 0;
    uint64_t base = physical & ~0xfffull;
    vm_offset_t address = ml_io_map(base, 0x1000);
    if (!address) return 0;
    maps[map_count].physical = base; maps[map_count].virtual = address; maps[map_count++].size = 0x1000;
    return address + physical - base;
}
bool lilac_a540_abort(arm_saved_state_t *state, uint32_t esr, vm_offset_t address)
{
    uintptr_t pc = get_saved_state_pc(state);
    bool instruction = (esr & 64) ? (pc == (uintptr_t)lilac_a540_store_pc ||
        (pc == (uintptr_t)lilac_a540_store64_pc && !(address & 7))) : pc == (uintptr_t)lilac_a540_load_pc;
    if (!probing || ((esr >> 26) & 63) != 0x25 || (address & 3) ||
        address != get_saved_state_reg(state, 0) || !instruction) return false;
    for (uint32_t i = 0; i < map_count; i++) {
        if (address >= maps[i].virtual && address - maps[i].virtual < maps[i].size) {
            fault_esr = esr; fault_address = maps[i].physical + address - maps[i].virtual;
            set_saved_state_pc(state, (uintptr_t)lilac_a540_recover_pc);
            return true;
        }
    }
    return false;
}
static int read32(__unused void *context, uint64_t physical, uint32_t *value)
{
    vm_offset_t address = address_of(physical);
    return address && lilac_a540_load32(address, value);
}
static int write32(__unused void *context, uint64_t physical, uint32_t value)
{
    vm_offset_t address = address_of(physical);
    return address && lilac_a540_store32(address, value);
}
static void wait_us(__unused void *context, uint32_t us) { delay_for_interval(us, NSEC_PER_USEC); }
static int clock_read(__unused void *context, uint32_t offset, uint32_t *value) { return read32(NULL,0x05065000 + offset,value); }
static int clock_write(__unused void *context, uint32_t offset, uint32_t value) { return write32(NULL,0x05065000 + offset,value); }
/* CB0 is disabled and no stream points at it. Change its inactive TTBR/TCR
 * and GR1 format fields, verify each, then restore. No DMA is enabled. */
static int smmu_writable(void)
{
    /* CBAR is not a reversible scratch register on this firmware: a valid
     * stage-1 assignment cannot be written back to the initial zero value.
     * Assign it only when setting up the real translation context. */
    const uint16_t offsets[] = {0x1800, 0x8010, 0x8030, 0x8020, 0x8024, 0x8038};
    const uint32_t values[] = {1, 0x38011, 0x803519, 0x12345000, 0, 0xf404ff44};
    uint32_t saved[6], value = 0;
    for (unsigned i = 0; i < 3; i++) {
        if (!read32(NULL,0x05040800 + 4*i,&value) || value ||
            !read32(NULL,0x05040c00 + 4*i,&value) || value != 0x200ff) return 0;
    }
    if (!read32(NULL,0x05048000,&value) || value) return 0;
    for (unsigned i = 0; i < 6; i++)
        if (!read32(NULL,0x05040000 + offsets[i],&saved[i])) return 0;
    unsigned changed = 0;
    int accepted = 1, restored = 1;
    for (unsigned i = 0; i < 6; i++) {
        changed = i + 1;
        if (!write32(NULL,0x05040000 + offsets[i],values[i]) ||
            !read32(NULL,0x05040000 + offsets[i],&value) || value != values[i]) {
            printf("lilac A540 SMMU: write rejected +%x expected=%x read=%x\n", offsets[i], values[i], value);
            accepted = 0; break;
        }
    }
    while (changed) {
        unsigned i = --changed;
        if (!write32(NULL,0x05040000 + offsets[i],saved[i]) ||
            !read32(NULL,0x05040000 + offsets[i],&value) || value != saved[i]) {
            printf("lilac A540 SMMU: restore +%x expected=%x read=%x\n", offsets[i],saved[i],value);
            restored = 0;
        }
    }
    printf("lilac A540 SMMU: inactive context writable=%u restored=%u\n", accepted, restored);
    return accepted && restored;
}

extern uint64_t kvtophys(vm_offset_t);
extern const uint8_t *hakios_image_file(const char *, const char *, uint64_t *, int *);
static vm_offset_t cp_tables, cp_ring, cp_fence;
static uint32_t cp_bus_before[2];
static unsigned cp_buses;
static bool cp_context;
static const uint64_t cp_bus_address[] = {0x00146040, 0x00171010};
static struct lilac_a5xx_ring cp_commands;
static bool zap_pending;
static bool render_domains;
static bool gpu_kept, gpu_failed, gpu_service;
static uint32_t gpu_sequence;
uint32_t gpu_log;
static vm_offset_t user_tables;
static struct lilac_gpu_va gpu_va;
static struct lilac_pm8005 retained_pm;
static struct lilac_a540_power retained_power;
static uint32_t retained_memacc;
extern void HakiosGPUStart(void), HakiosGPUStop(void);
extern int lilac_a540_cx_release(void);

static const uint8_t *gpu_firmware(const char *name, uint64_t *size)
{
    const uint8_t *data = hakios_image_file("fw",name,size,NULL);
    return data ? data : hakios_image_file("probe/gpu-firmware",name,size,NULL);
}

static void cache_flush(vm_offset_t memory, uint64_t size)
{
    for (vm_offset_t line = memory & ~63ull; line < memory + size; line += 64)
        __asm__ volatile("dc civac, %0" :: "r"(line) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}
static int gpu_write(uint32_t reg, uint32_t value) { return write32(NULL,0x05000000ull + 4ull*reg,value); }
static int gpu_read(uint32_t reg, uint32_t *value) { return read32(NULL,0x05000000ull + 4ull*reg,value); }
static int domain_set(uint32_t control, uint32_t status, bool on)
{
    if (!gpu_write(control,on ? 0x778000 : 0x778001)) return 0;
    wait_us(NULL,3);
    uint32_t value = 0;
    for (unsigned i = 0; i < 1000; i++) {
        if (!gpu_read(status,&value)) return 0;
        if (!!(value & (1u << 20)) == on) return 1;
        wait_us(NULL,1);
    }
    printf("lilac A540 domains: control=%x on=%u status=%x\n",control,on,value);
    return 0;
}
static int smmu_set(uint32_t offset, uint32_t value)
{
    uint32_t actual = 0;
    if (write32(NULL,0x05040000 + offset,value) && read32(NULL,0x05040000 + offset,&actual) && actual == value) return 1;
    printf("lilac A540 MMU: +%x expected=%x actual=%x\n",offset,value,actual);
    return 0;
}
static int cp_buffer(uint64_t address, uint32_t bytes, const uint8_t *source, vm_offset_t *memory)
{
    if (!bytes || address < 0x02000000 || address + bytes > 0x02200000 ||
        kmem_alloc(kernel_map,memory,round_page(bytes),KMA_KOBJECT | KMA_ZERO | KMA_DATA,VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) return 0;
    if (source) memcpy((void *)*memory,source,bytes);
    uint64_t *leaves = (uint64_t *)(cp_tables + 8192);
    for (uint32_t at = 0; at < bytes; at += 4096)
        leaves[((address + at) >> 12) & 511] = kvtophys(*memory + at) | 0xe43ull;
    cache_flush(*memory,round_page(bytes));
    return 1;
}
static int cp_memory(void)
{
    uint64_t pm4_size = 0, pfp_size = 0;
    const uint8_t *pm4 = gpu_firmware("a530_pm4.fw",&pm4_size);
    const uint8_t *pfp = gpu_firmware("a530_pfp.fw",&pfp_size);
    if (!pm4 || !pfp || pm4_size < 16 || pm4_size > 32768 || pfp_size < 16 || pfp_size > 32768 ||
        ((pm4_size | pfp_size) & 3) || PAGE_SIZE < 16384) return 0;
    if (kmem_alloc(kernel_map,&cp_tables,PAGE_SIZE,KMA_KOBJECT | KMA_ZERO | KMA_DATA,VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) return 0;
    uint64_t *root = (uint64_t *)cp_tables, *middle = (uint64_t *)(cp_tables + 4096);
    root[0] = kvtophys(cp_tables + 4096) | 3;
    middle[16] = kvtophys(cp_tables + 8192) | 3;
    vm_offset_t code;
    if (!cp_buffer(0x02000000,32768,NULL,&cp_ring) || !cp_buffer(0x02008000,(uint32_t)pm4_size - 4,pm4 + 4,&code) ||
        !cp_buffer(0x02010000,(uint32_t)pfp_size - 4,pfp + 4,&code) || !cp_buffer(0x02018000,4096,NULL,&cp_fence)) return 0;
    cache_flush(cp_tables,PAGE_SIZE);
    cp_commands = (struct lilac_a5xx_ring){(uint32_t *)cp_ring,8192,0};
    printf("lilac A540 CP: firmware PM4=%llu PFP=%llu, isolated 4K tables root=%llx\n",pm4_size,pfp_size,kvtophys(cp_tables));
    printf("lilac A540 MMU: L1=%llx L2=%llx PTE0=%llx ring physical=%llx\n",
        root[0],middle[16],((uint64_t *)(cp_tables + 8192))[0],kvtophys(cp_ring));
    return 1;
}
static int user_memory(void)
{
    /* Reserve all leaf tables up front: no allocator or table replacement
     * races with the device after service publication. Only CB0 is used. */
    const uint32_t bytes = 480*4096;
    if (kmem_alloc(kernel_map,&user_tables,round_page(bytes),KMA_KOBJECT | KMA_ZERO | KMA_DATA,VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) return 0;
    uint64_t *middle = (uint64_t *)(cp_tables+4096);
    for (unsigned i = 32; i < 512; i++) middle[i] = kvtophys(user_tables+(i-32)*4096) | 3;
    cache_flush(user_tables,bytes); cache_flush(cp_tables,PAGE_SIZE);
    return 1;
}
static int cp_mmu_on(void)
{
    uint64_t root = kvtophys(cp_tables);
    cp_context = true; /* cleanup is required even after a partial setup */
    if (!smmu_set(0x8000,0) || !smmu_set(0x1800,1) || !smmu_set(0x1000,0x1f300) ||
        !smmu_set(0x8010,0x38011) || !smmu_set(0x8030,0x803519) ||
        !smmu_set(0x8020,(uint32_t)root) || !smmu_set(0x8024,(uint32_t)(root >> 32)) ||
        !smmu_set(0x8028,0) || !smmu_set(0x802c,0) || !smmu_set(0x8038,0xf404ff44) || !smmu_set(0x803c,0) ||
        !write32(NULL,0x05048058,0xc00001fe) || !write32(NULL,0x05048610,0) || !write32(NULL,0x050487f0,1)) return 0;
    uint32_t status = 1;
    for (unsigned i = 0; i < 1000; i++) {
        if (!read32(NULL,0x050487f4,&status)) return 0;
        if (!(status & 1)) break;
        wait_us(NULL,1);
    }
    if ((status & 1) || !smmu_set(0x8000,0x1127) || !smmu_set(0xc00,0) || !smmu_set(0x800,0x80000000)) return 0;
    printf("lilac A540 MMU: stream 0 translates through context 0\n");
    vm_offset_t atos = address_of(0x05048800);
    if (atos && lilac_a540_store64(atos,0x02000000)) {
        uint32_t busy = 1, low = 0, high = 0;
        for (unsigned i = 0; i < 1000; i++) {
            if (!read32(NULL,0x050488f0,&busy) || !(busy & 1)) break;
            wait_us(NULL,1);
        }
        read32(NULL,0x05048050,&low); read32(NULL,0x05048054,&high);
        printf("lilac A540 MMU: ATOS ring busy=%x PAR=%x:%x expected=%llx\n",busy,high,low,kvtophys(cp_ring));
    }
    return 1;
}
static void cp_diagnostics(const char *phase)
{
    static const uint16_t regs[] = {0x800,0x801,0x802,0x804,0x805,0x806,0x807,0x835,0x836,0x838,0x839,0x832,0x860,0x4f5,0x4e1,0xb18,0xb1a,0xb1c};
    printf("lilac A540 CP: %s\n",phase);
    for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        uint32_t value = 0;
        int ok = gpu_read(regs[i],&value);
        printf("lilac A540 CP: reg[%x]=%x readable=%u\n",regs[i],value,ok);
    }
    const uint16_t offsets[] = {0x48,0x8000,0x8010,0x8020,0x8024,0x8030,0x8058,0x8060,0x8064,0x8068};
    for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
        uint32_t value = 0;
        if (read32(NULL,0x05040000 + offsets[i],&value)) printf("lilac A540 MMU: +%x=%x\n",offsets[i],value);
    }
}
static void cp_phase(const char *phase)
{
    uint32_t fsr = 0, far = 0, status = 0;
    read32(NULL,0x05048058,&fsr); read32(NULL,0x05048060,&far);
    gpu_read(0x4f5,&status);
    printf("lilac A540 CP: %s FSR=%x FAR=%x RBBM=%x\n",phase,fsr,far,status);
}
static int cp_send(void)
{
    cache_flush(cp_ring,32768);
    if (!gpu_write(0x807,(uint32_t)cp_commands.write_index)) return 0;
    uint32_t read = ~0u;
    for (unsigned i = 0; i < 20000; i++) {
        if (!gpu_read(0x806,&read)) return 0;
        if (read == cp_commands.write_index) break;
        wait_us(NULL,100);
    }
    if (read != cp_commands.write_index) return 0;
    /* Reading CP_RB_RPTR itself wakes the CP bus and busy hysteresis. Once
     * consumed, poll RBBM alone, as a5xx_idle does, so it can become idle. */
    for (unsigned i = 0; i < 20000; i++) {
        uint32_t status;
        if (!gpu_read(0x4f5,&status)) return 0;
        if (!(status & ~1u)) return 1;
        wait_us(NULL,100);
    }
    return 0;
}
static int32_t zap_scm(uint32_t function, uint64_t arguments, uint64_t second, uint64_t third, uint64_t *answer)
{
    register uint64_t x0 __asm__("x0") = 0x42000000ull | function;
    register uint64_t x1 __asm__("x1") = arguments;
    register uint64_t x2 __asm__("x2") = 13;
    register uint64_t x3 __asm__("x3") = second;
    register uint64_t x4 __asm__("x4") = third;
    register uint64_t x5 __asm__("x5") = 0;
    register uint64_t x6 __asm__("x6") = 0;
    *answer = ~0ull;
    for (unsigned i = 0; i < 100; i++) {
        /* SCM_INTERRUPTED resumes with every returned register, including
         * x6's session token, exactly as Sony's __scm_call_armv8_64 does. */
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
            "+r"(x4), "+r"(x5), "+r"(x6) : :
            "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
        if (x0 != 1) {
            *answer = x1;
            int32_t result = (int32_t)x0;
            printf("lilac A540 ZAP: PAS %x result=%d answer=%llx resumes=%u\n",function,result,x1,i);
            return result;
        }
    }
    printf("lilac A540 ZAP: PAS %x remains interrupted; memory retained until reboot\n",function);
    return -1;
}
static int zap_load(void)
{
    uint64_t header_size = 0, code_size = 0, answer = ~0ull;
    const uint8_t *header = gpu_firmware("a540_zap.mdt",&header_size);
    const uint8_t *code = gpu_firmware("a540_zap.b02",&code_size);
    struct lilac_a540_zap_layout layout;
    if (!lilac_a540_zap_layout(header,header_size,code_size,&layout) || !code || PAGE_SIZE < 16384) return 0;
    vm_offset_t metadata = 0, image = 0;
    if (kmem_alloc(kernel_map,&metadata,PAGE_SIZE,KMA_KOBJECT | KMA_ZERO | KMA_DATA,VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS ||
        kmem_alloc_contig(kernel_map,&image,layout.alignment,PAGE_MASK,0,(layout.alignment - 1) >> PAGE_SHIFT,
            KMA_KOBJECT | KMA_ZERO | KMA_DATA,VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) return 0;
    uint64_t physical = kvtophys(image);
    if (physical & (layout.alignment - 1)) return 0;
    memcpy((void *)metadata,header,header_size);
    cache_flush(metadata,PAGE_SIZE); cache_flush(image,layout.alignment);
    printf("lilac A540 ZAP: metadata=%llx image=%llx extent=%u reserved=%u\n",
        kvtophys(metadata),physical,layout.memory_bytes,layout.alignment);
    /* These dedicated pages remain wired for this boot, including rejected
     * and interrupted calls. Never access secure-owned image memory after
     * authentication, and never interpret an error as an existing shader. */
    if (zap_scm(0x206,1,0,0,&answer) || answer) return 0;
    if (zap_scm(0x201,0x82,kvtophys(metadata),0,&answer) || answer) return 0;
    zap_pending = true;
    if (zap_scm(0x202,3,physical,layout.memory_bytes,&answer) || answer) return 0;
    memcpy((void *)image,code,layout.file_bytes);
    cache_flush(image,PAGE_SIZE);
    if (zap_scm(0x205,1,0,0,&answer) || answer) return 0;
    printf("lilac A540 ZAP: authenticated image 13, dedicated memory=%llx extent=%u reserved=%u\n",physical,layout.memory_bytes,layout.alignment);
    return 1;
}
static int render_setup(void)
{
    /* Linux v6.12 a5xx_hw_init, A540 1 MiB GMEM, Sony highest-bank-bit=15.
     * Domains stay manually on. Autonomous collapse/DVFS remains disabled. */
    const uint32_t setup[][2] = {
        {0xe96,2}, {0xe8b,0x100000}, {0xe8c,0}, {0xe8d,0x1fffff}, {0xe8e,0},
        {0xd00,(0x400u << 11) | (0x300u << 22)}, {0xf02,2u << 7}, {0xcc6,2u << 1}, {0xe8f,2},
        {0xf803,0}, {0xf800,0}, {0xf801,0}, {0xf802,0},
        {0xbc1,1}, {0xc81,1}, {0xcc5,1}, {0xd01,1}, {0xe05,1}, {0xe41,1},
        {0xe61,1}, {0xec1,1}, {0xf01,1}, {0xf810,1},
    };
    for (unsigned i = 0; i < sizeof(setup)/sizeof(setup[0]); i++)
        if (!gpu_write(setup[i][0],setup[i][1])) return 0;
    uint32_t value;
    if (!gpu_read(0xe60,&value) || !gpu_write(0xe60,value | (1u << 10) | (1u << 23)) ||
        !gpu_read(0xe04,&value) || !gpu_write(0xe04,value & ~(1u << 18))) return 0;
    const uint32_t protection[][2] = {
        {4,2},{8,3},{16,4},{32,5},{64,6},{128,6},{0xf800,4},{0xf400,1},
        {0x800,6},{0x840,3},{0x880,5},{0xaa0,0},{0xcc0,0},{0xcf0,1},
        {0xe68,3},{0xe70,4},{0xe80,4},{0x10000,15},
    };
    for (unsigned i = 0; i < sizeof(protection)/sizeof(protection[0]); i++)
        if (!gpu_write(0x880+i,0x60000000 | (protection[i][1] << 24) | (protection[i][0] << 2))) return 0;
    return gpu_write(0x8a0,7);
}
static int fence_smoke(void)
{
    vm_offset_t commands;
    if (!cp_buffer(0x0201c000,4096,NULL,&commands)) return 0;
    /* Install before starting this IB and invalidate the context's TLB. */
    cache_flush(cp_tables,PAGE_SIZE);
    if (!write32(NULL,0x05048610,0) || !write32(NULL,0x050487f0,1)) return 0;
    uint32_t *ib = (uint32_t *)commands;
    struct lilac_a5xx_ring stream = {ib,1024,0};
    const uint32_t payload[] = {0x02018010,0,0x540f00d};
    if (!lilac_a5xx_ring_packet7(&stream,0,0x3d,payload,3)) return 0;
    cache_flush(commands,4096);
    const struct lilac_a5xx_ib work = {0x0201c000,(uint32_t)stream.write_index};
    if (!lilac_a5xx_submit(&cp_commands,cp_commands.write_index,&work,1,0x02018020,1) || !cp_send()) return 0;
    cache_flush(cp_fence,4096);
    uint32_t data = *(volatile uint32_t *)(cp_fence+16), fence = *(volatile uint32_t *)(cp_fence+32);
    printf("lilac A540 submit: IB data=%x CACHE_FLUSH_TS fence=%u\n",data,fence);
    return data == 0x540f00d && fence == 1;
}
static int cp_stop(void)
{
    if (!gpu_write(0x3080,0xf)) return 0;
    uint32_t halted = 0;
    for (unsigned i = 0; i < 1000; i++) {
        if (!gpu_read(0x3081,&halted)) return 0;
        if ((halted & 15) == 15) break;
        wait_us(NULL,10);
    }
    if ((halted & 15) != 15) return 0;
    /* A540 must not use the A530v1 CP halt bits or its VBIF block reset.
     * Once requests are drained, reset the core as a5xx_recover does; its
     * reset state keeps both firmware engines stopped until configured. */
    uint32_t reset;
    if (!gpu_write(0x43,1) || !gpu_read(0x43,&reset) || !gpu_write(0x43,0)) return 0;
    wait_us(NULL,10);
    uint32_t status = ~0u;
    if (!gpu_read(0x4f5,&status) || (status & ~1u)) return 0;
    if (zap_pending) {
        uint64_t answer;
        if (zap_scm(0x206,1,0,0,&answer) || answer) return 0;
        zap_pending = false;
    }
    if (render_domains) {
        if (!domain_set(0xa881,0xa88b,false) || !domain_set(0xa887,0xa88d,false)) return 0;
        render_domains = false;
    }
    return 1;
}
/* Returns whether it is safe to collapse GX, independently of test success. */
static int cp_smoke(uint32_t stage)
{
    if (!cp_memory()) { printf("lilac A540 CP: missing firmware or allocation failed\n"); return 1; }
    if (stage >= 7 && !user_memory()) return 1;
    for (unsigned i = 0; i < 2; i++) {
        if (!read32(NULL,cp_bus_address[i],&cp_bus_before[i])) return 1;
        cp_buses = i + 1;
        if (!write32(NULL,cp_bus_address[i],cp_bus_before[i] | 1)) return 1;
        uint32_t value;
        if (!read32(NULL,cp_bus_address[i],&value) || !(value & 1)) return 1;
    }
    if (!cp_mmu_on()) return 1;
    if (stage >= 5) {
        render_domains = true;
        if (!domain_set(0xa887,0xa88d,true) || !domain_set(0xa881,0xa88b,true)) return cp_stop();
        printf("lilac A540 domains: RBCCU and SP powered on\n");
    }
    cp_phase("MMU enabled before CP setup");
    /* Single ring, no preemption, no interrupts, no autonomous power control.
     * A540-specific CP thresholds and 64-bit fetch registers, Linux a5xx_hw_init. */
    const uint32_t setup[][2] = {
        {0x38,0}, {0x93,1}, {0x94,0xa6ffffff}, {0x95,0x3f},
        {0x3049,3}, {0x302a,9},
        {0x825,0x40}, {0x826,0x400}, {0x820,0x80000060}, {0x81f,0x40201b16},
        {0x833,0x02000000}, {0x860,1}, {0xe80,1}, {0xe81,1u << 29},
        {0xe89,0xffff0000}, {0xe8a,0x1ffff}, {0xe87,0xffff0000}, {0xe88,0x1ffff},
        {0x838,0x02008000}, {0x839,0}, {0x835,0x02010000}, {0x836,0},
        {0x800,0x02000000}, {0x801,0}, {0x802,0x0800020c}, {0x804,0x02018008}, {0x805,0},
    };
    for (unsigned i = 0; i < sizeof(setup)/sizeof(setup[0]); i++)
        if (!gpu_write(setup[i][0],setup[i][1])) return cp_stop();
    if (stage >= 6 && !render_setup()) return cp_stop();
    cp_phase("registers configured before CP start");
    if (!lilac_a540_me_init(&cp_commands,0) || !gpu_write(0x832,0)) return cp_stop();
    wait_us(NULL,1000);
    cp_phase("CP unhalted before doorbell");
    if (!cp_send()) {
        cp_diagnostics("ME_INIT failed"); return cp_stop();
    }
    printf("lilac A540 CP: ME_INIT completed on hardware\n");
    const uint32_t write[] = {0x02018000,0,0xa540c0de};
    if (!lilac_a5xx_ring_packet7(&cp_commands,cp_commands.write_index,0x3d,write,3) || !cp_send()) {
        cp_diagnostics("MEM_WRITE failed"); return cp_stop();
    }
    cache_flush(cp_fence,4096);
    printf("lilac A540 CP: DMA MEM_WRITE expected=a540c0de actual=%x\n",*(volatile uint32_t *)cp_fence);
    if (*(volatile uint32_t *)cp_fence != 0xa540c0de) return cp_stop();
    if (stage >= 5) {
        if (!zap_load()) { printf("lilac A540 ZAP: authentication not established\n"); return cp_stop(); }
        const uint32_t mode = 0;
        if (!lilac_a5xx_ring_packet7(&cp_commands,cp_commands.write_index,0x66,&mode,1) || !cp_send()) {
            cp_diagnostics("secure mode transition failed"); return cp_stop();
        }
        printf("lilac A540 CP: left secure mode through authenticated shader\n");
    }
    if (stage >= 6 && !fence_smoke()) { cp_diagnostics("IB/fence failed"); return cp_stop(); }
    if (stage >= 7) {
        *(volatile uint32_t *)(cp_fence+64) = 0;
        cache_flush(cp_fence,4096);
        gpu_kept = true;
        printf("lilac A540: ready for Mesa, 1 MiB GMEM, fixed 19.2 MHz, single ring\n");
        return 1;
    }
    cp_diagnostics("completion");
    return cp_stop();
}
static int cp_cleanup(void)
{
    int ok = 1;
    /* GX is already off: no DMA can race these translation changes. Pages
     * stay wired for this diagnostic boot even if a cleanup fails. */
    if (cp_context) {
        if (!smmu_set(0x800,0)) ok = 0;
        if (!smmu_set(0x8000,0)) ok = 0;
        /* Firmware accepts assignment to CB0 but rejects restoring S2CR's
         * fault type and CBAR's initial zero. Invalidate the matching entry
         * and independently disable translation; do not claim those routing
         * fields were restored. GX has already stopped and collapsed. */
        uint32_t route = 0, attributes = 0;
        read32(NULL,0x05040c00,&route); read32(NULL,0x05041000,&attributes);
        printf("lilac A540 MMU: stream invalid, CB0 disabled=%u; retained S2CR=%x CBAR=%x\n",ok,route,attributes);
    }
    while (cp_buses) {
        unsigned i = --cp_buses;
        uint32_t after;
        if (!write32(NULL,cp_bus_address[i],cp_bus_before[i]) || !read32(NULL,cp_bus_address[i],&after) ||
            (after & 1) != (cp_bus_before[i] & 1)) ok = 0;
    }
    return ok;
}

int lilac_a540_probe(void)
{
    uint32_t stage = 0;
    int release_cx = 1;
    if (!PE_parse_boot_argn("lilac_a540", &stage, sizeof(stage)) || stage < 1 || stage > 7) return 1;
    probing = true;
    if (stage >= 2) smmu_writable();
    const struct lilac_pm8005_io io = {NULL,read32,write32,wait_us};
    struct lilac_pm8005 pm;
    int pm_ok = lilac_pm8005_snapshot(&io,&pm);
    printf("lilac A540 PM8005: readable=%u arb=%x channel=%u type=%x subtype=%x mv=%u mode=%x enable=%x status=%x transaction=%x\n",
        pm_ok,pm.version,pm.channel,pm.type,pm.subtype,pm.millivolts,pm.mode,pm.enable,pm.status,pm.last_status);
    if (!pm_ok) goto done;
    uint32_t rev, limit, force, gx, clamp, config, command, core, timer, timer_cfg, timer_cmd, memacc;
    if (!read32(NULL,0x0078413c,&rev) || !read32(NULL,0x0078414c,&limit) ||
        !read32(NULL,0x00784324,&force) || !read32(NULL,0x05066094,&gx) ||
        !read32(NULL,0x05065130,&clamp) || !read32(NULL,0x05066074,&config) ||
        !read32(NULL,0x05066070,&command) || !read32(NULL,0x05066098,&core) ||
        !read32(NULL,0x050660d0,&timer) || !read32(NULL,0x050660b4,&timer_cfg) ||
        !read32(NULL,0x050660b0,&timer_cmd) || !read32(NULL,0x01fcf004,&memacc)) goto done;
    rev = (rev >> 16) & 7; limit = (limit >> 14) & 3; force = (force >> 13) & 1;
    printf("lilac A540 GX: fuse_rev=%u limit=%u force_high=%u gdsc=%x clamp=%x RCG=%x/%x core=%x timer=%x/%x/%x memacc=%x\n",
        rev,limit,force,gx,clamp,command,config,core,timer,timer_cmd,timer_cfg,memacc);
    if (stage < 3) goto done;
    /* Borrow an already enabled rail without changing it. Following a clean
     * Android shutdown it is off: take our own fixed, corner-1 vote and
     * restore it only after GX collapse. No closed-loop CPR or DVFS. */
    if (force || limit == 2 || (gx & 0x80000003u) != 1 || (clamp & 17) != 1 || ((core | timer) & 1) ||
        ((pm.enable & 0x80) && (!(pm.status & 0x80) || pm.millivolts < (rev ? 724 : 716) || pm.millivolts > 1088)) ||
        (!(pm.enable & 0x80) && (pm.status & 0x80))) {
        printf("lilac A540 GX: regulator cannot support low-frequency operation\n"); goto done;
    }
    if (!lilac_rpm_connect(true) || !lilac_rpm_gpu_vote(0x80)) goto done;
    if (!(pm.enable & 0x80) && !lilac_pm8005_on(&io,&pm,rev ? 724 : 716)) {
        int restored = lilac_pm8005_restore(&io,&pm);
        int released = lilac_rpm_gpu_vote(0);
        printf("lilac A540 GX: regulator enable failed, restored=%u MX released=%u\n",
            restored,released);
        goto done;
    }
    printf("lilac A540 GX: regulator ready, owned=%u target_mv=%u\n",pm.touched,pm.touched ? (rev ? 724 : 716) : pm.millivolts);
    const struct lilac_a540_power_io power_io = {NULL,clock_read,clock_write,wait_us};
    struct lilac_a540_power power = {0};
    uint32_t actual = 0;
    int acquired = 0;
    if (write32(NULL,0x01fcf004,memacc | 1) && read32(NULL,0x01fcf004,&actual) && actual == (memacc | 1)) {
        acquired = lilac_a540_power_acquire(&power_io,&power);
    }
    printf("lilac A540 GX: acquired=%u last_offset=%x value=%x memacc=%x\n",
        acquired,power.last_offset,power.last_value,actual);
    if (acquired) {
        uint32_t interrupts = 0, status = 0;
        /* A5xx register indices 0x4f5 and 0x4e1, converted to bytes. */
        int readable = read32(NULL,0x050013d4,&status) && read32(NULL,0x05001384,&interrupts);
        printf("lilac A540 GX: GPU readable=%u RBBM_STATUS=%x INT_0_STATUS=%x\n",readable,status,interrupts);
        if (readable && stage >= 4 && !cp_smoke(stage)) {
            printf("lilac A540 CP: bus halt unconfirmed; retaining power and mappings for reboot\n");
            release_cx = 0; goto done;
        }
        if (gpu_kept) {
            retained_pm = pm; retained_power = power; retained_memacc = memacc;
            release_cx = 0; goto done;
        }
    }
    if (!lilac_a540_power_release(&power_io,&power)) {
        release_cx = 0;
        printf("lilac A540 GX: cleanup unconfirmed at +%x=%x; retaining MX and CX for reboot\n",
            power.last_offset,power.last_value); goto done;
    }
    if (!write32(NULL,0x01fcf004,memacc) || !read32(NULL,0x01fcf004,&actual) || actual != memacc)
        printf("lilac A540 GX: memacc restore failed while GX is off\n");
    if (stage >= 4) printf("lilac A540 CP: stopped and translation/bus cleanup=%u\n",cp_cleanup());
    int pm_restored = lilac_pm8005_restore(&io,&pm);
    int mx_released = lilac_rpm_gpu_vote(0);
    printf("lilac A540 GX: collapsed, clocks restored, PM8005 restored=%u MX released=%u\n",pm_restored,mx_released);

done:
    printf("lilac A540 backend: stage=%u ESR=%x physical=%llx\n",stage,fault_esr,(uint64_t)fault_address);
    probing = false;
    return release_cx;
}

/* HakiosGPU serializes these entry points. A failed device remains owned so
 * the service pins outstanding DMA buffers until a confirmed stop or reboot. */
bool hakios_gpu_ready(void) { return gpu_kept; }
void hakios_adreno_init(void)
{
    if (gpu_kept && !gpu_service) {
        PE_parse_boot_argn("gpu_log",&gpu_log,sizeof(gpu_log));
        gpu_service = true;
        HakiosGPUStart();
    }
}
bool hakios_gpu_parameter(uint32_t which, uint64_t *value)
{
    if (!value) return false;
    switch (which) {
    case 1: *value = 540; break;
    case 2: *value = 1024*1024; break;
    case 3: *value = 0x05040001; break;
    case 4: *value = 19200000; break;
    case 5: *value = mach_absolute_time(); break;
    case 6: *value = 0x100000; break;
    case 7: *value = 1; break;
    case 8: case 9: case 10: case 0x11: *value = 0; break;
    case 0x10: *value = 15; break;
    default: return false;
    }
    return true;
}
uint64_t hakios_gpu_address(uint64_t size)
{
    return gpu_kept && !gpu_failed ? lilac_gpu_va_alloc(&gpu_va,size) : 0;
}
void hakios_gpu_address_free(uint64_t address, uint64_t size)
{
    if (!lilac_gpu_va_free(&gpu_va,address,size)) printf("lilac A540: invalid VA free %llx/%llx\n",address,size);
}
bool hakios_gpu_map(uint64_t address, uint64_t phys, bool read_only)
{
    if (!gpu_kept || gpu_failed || !phys || ((address | phys) & 4095) || phys >= (1ull << 40) ||
        !lilac_gpu_va_contains(&gpu_va,address,4096)) return false;
    uint64_t *entry = (uint64_t *)user_tables+(address-LILAC_GPU_VA_START)/4096;
    if (*entry) return false;
    *entry = phys | 0xe43ull | (read_only ? 0x80 : 0);
    cache_flush((vm_offset_t)entry,8);
    return true;
}
static int user_tlb_sync(void)
{
    if (!write32(NULL,0x05048610,0) || !write32(NULL,0x050487f0,1)) return 0;
    for (unsigned i = 0; i < 1000; i++) {
        uint32_t status;
        if (!read32(NULL,0x050487f4,&status)) return 0;
        if (!(status & 1)) return 1;
        wait_us(NULL,1);
    }
    return 0;
}
void hakios_gpu_mapped(void)
{
    if (!user_tlb_sync()) { gpu_failed = true; printf("lilac A540: TLB sync failed; submissions disabled\n"); }
}
void hakios_gpu_unmap(uint64_t address, uint64_t size)
{
    if ((address | size) & 4095 || !lilac_gpu_va_contains(&gpu_va,address,size)) return;
    uint64_t *entry = (uint64_t *)user_tables+(address-LILAC_GPU_VA_START)/4096;
    bzero(entry,(size/4096)*8);
    cache_flush((vm_offset_t)entry,(size/4096)*8);
    hakios_gpu_mapped();
}
void hakios_gpu_to_memory(void *start, uint64_t size) { cache_flush((vm_offset_t)start,size); }
bool hakios_gpu_done(uint32_t fence)
{
    if (!fence) return true;
    if (!cp_fence || (int32_t)(gpu_sequence-fence) < 0) return false;
    cache_flush(cp_fence+64,4);
    return (int32_t)(*(volatile uint32_t *)(cp_fence+64)-fence) >= 0;
}
uint32_t hakios_gpu_last(void) { return gpu_sequence; }
void hakios_gpu_worked(uint64_t *us, uint32_t *submissions, uint32_t by[7])
{
    /* Do not label CPU wait time as hardware execution time. */
    *us = 0; *submissions = 0; bzero(by,7*sizeof(*by));
}
uint32_t hakios_gpu_submit(const uint64_t *addresses, const uint32_t *words, uint32_t count)
{
    if (!gpu_kept || gpu_failed || !addresses || !words || !count || count > LILAC_A5XX_SUBMIT_MAX) return 0;
    struct lilac_a5xx_ib ibs[LILAC_A5XX_SUBMIT_MAX];
    for (uint32_t i = 0; i < count; i++) {
        if (!words[i] || (addresses[i] & 3) || !lilac_gpu_va_contains(&gpu_va,addresses[i],(uint64_t)words[i]*4)) return 0;
        ibs[i] = (struct lilac_a5xx_ib){addresses[i],words[i]};
    }
    uint32_t read = 0, next = gpu_sequence+1;
    if (!next) next = 1;
    for (unsigned i = 0; i < 20000; i++) {
        if (!gpu_read(0x806,&read)) return 0;
        if (lilac_a5xx_submit(&cp_commands,read,ibs,count,0x02018040,next)) {
            cache_flush(cp_ring,32768);
            gpu_sequence = next;
            /* Once publication is attempted, this sequence owns its buffers
             * even on failure. An unsignaled fence pins them in the service. */
            if (!gpu_write(0x807,(uint32_t)cp_commands.write_index)) { gpu_failed = true; return next; }
            if (gpu_sequence <= 8 || gpu_log) printf("lilac A540 submit: sequence=%u IBs=%u words0=%u\n",next,count,words[0]);
            return next;
        }
        wait_us(NULL,100);
    }
    return 0;
}
void hakios_gpu_report(const char *why)
{
    gpu_failed = true;
    printf("lilac A540: submissions disabled: %s last=%u\n",why,gpu_sequence);
    cp_diagnostics("userspace submission failed");
    const uint16_t regs[] = {0xb1f,0xb20,0xb21,0xb22,0xb23,0xb24};
    for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        uint32_t value = 0;
        gpu_read(regs[i],&value); printf("lilac A540 IB: reg[%x]=%x\n",regs[i],value);
    }
}
void hakios_adreno_minute(void) {}
void hakios_adreno_quiesce(void)
{
    if (!gpu_kept) return;
    gpu_failed = true;
    /* Panic path: no service lock, allocation, PAS call or power release. */
    gpu_write(0x38,0); gpu_write(0x3080,15);
    for (unsigned i = 0; i < 1000; i++) {
        uint32_t halted = 0;
        if (!gpu_read(0x3081,&halted) || (halted & 15) == 15) break;
        wait_us(NULL,10);
    }
}
void hakios_gpu_stop(void)
{
    if (!gpu_kept) return;
    gpu_failed = true;
    for (unsigned i = 0; i < 5000 && !hakios_gpu_done(gpu_sequence); i++) wait_us(NULL,100);
    /* A completed fence can precede the busy hysteresis becoming idle. Let
     * it settle before recording the final state; don't read CP here. */
    for (unsigned i = 0; i < 1000; i++) {
        uint32_t status = ~0u;
        if (!gpu_read(0x4f5,&status) || !(status & ~1u)) break;
        wait_us(NULL,1);
    }
    cp_phase("final render state");
    uint32_t interrupt = 0, fault = 0, protect = 0;
    gpu_read(0xb18,&interrupt); gpu_read(0xb1a,&fault); gpu_read(0xb1c,&protect);
    printf("lilac A540 render faults: CP interrupt=%x hardware=%x protection=%x\n",interrupt,fault,protect);
    if (!cp_stop()) { printf("lilac A540 stop: halt unconfirmed; retaining all resources\n"); return; }
    gpu_kept = false;
    const struct lilac_a540_power_io power_io = {NULL,clock_read,clock_write,wait_us};
    const struct lilac_pm8005_io io = {NULL,read32,write32,wait_us};
    if (!lilac_a540_power_release(&power_io,&retained_power)) {
        printf("lilac A540 stop: GX collapse unconfirmed; retaining MX/CX\n"); return;
    }
    uint32_t actual = 0;
    int memory_restored = write32(NULL,0x01fcf004,retained_memacc) && read32(NULL,0x01fcf004,&actual) && actual == retained_memacc;
    int mmu = cp_cleanup(), pm = lilac_pm8005_restore(&io,&retained_pm), mx = lilac_rpm_gpu_vote(0);
    int cx = lilac_a540_cx_release();
    printf("lilac A540 stop: sequence=%u completed=%u MMU=%u memory=%u PM8005=%u MX=%u CX=%u\n",
        gpu_sequence,*(volatile uint32_t *)(cp_fence+64),mmu,memory_restored,pm,mx,cx);
}
void hakios_adreno_stop(void) { if (gpu_kept && gpu_service) HakiosGPUStop(); }
