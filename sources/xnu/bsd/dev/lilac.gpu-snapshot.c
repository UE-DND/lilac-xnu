#include <mach/mach_types.h>
#include <kern/clock.h>
#include <arm/thread.h>
#include <pexpert/pexpert.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include "lilac-hardware.h"
#include "lilac.mmio-guard.h"
#include "../../../../kernel/lilac-a540-cx.h"
#include "../../../../kernel/lilac-rpm-wire.h"
#include "../../../../kernel/lilac-rpm-api.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void lilac_gpu_snapshot_init(void);
#ifdef HAKIOS_A540
int lilac_a540_probe(void);
#else
static int lilac_a540_probe(void) { return 1; }
#endif
int lilac_a540_cx_release(void);
int lilac_a540_cx_acquire(void);
bool lilac_gpu_abort(arm_saved_state_t *, uint32_t, vm_offset_t);
static struct lilac_gpu_snapshot gpu_snapshot = {.version = LILAC_HARDWARE_VERSION};
static struct lilac_a540_cx_result cx_result = {.result = LILAC_CX_BAD_ARGUMENT};
static struct lilac_gpu_tz_status tz_result;
static struct lilac_gpu_smmu_status smmu_result;
static vm_offset_t gpu_smmu, gpu_rpm;
static vm_offset_t retained_cx;
static uint32_t gpu_deep;
static uint32_t gpu_fault_esr;
static vm_offset_t gpu_fault_address;
SYSCTL_OPAQUE(_kern, OID_AUTO, lilac_gpu_snapshot, CTLFLAG_RD | CTLFLAG_LOCKED,
    &gpu_snapshot, sizeof(gpu_snapshot), "S,lilac_gpu_snapshot", "Read-only A540 clock and power snapshot");
SYSCTL_OPAQUE(_kern, OID_AUTO, lilac_gpu_cx_result, CTLFLAG_RD | CTLFLAG_LOCKED,
    &cx_result, sizeof(cx_result), "S,lilac_a540_cx_result", "A540 CX power vote experiment");
SYSCTL_OPAQUE(_kern, OID_AUTO, lilac_gpu_tz_status, CTLFLAG_RD | CTLFLAG_LOCKED,
    &tz_result, sizeof(tz_result), "S,lilac_gpu_tz_status", "Read-only GPU TrustZone capability queries");
SYSCTL_OPAQUE(_kern, OID_AUTO, lilac_gpu_smmu_status, CTLFLAG_RD | CTLFLAG_LOCKED,
    &smmu_result, sizeof(smmu_result), "S,lilac_gpu_smmu_status", "A540 SMMU identity probe");

extern int lilac_gpu_read32(vm_offset_t address, uint32_t *value);
extern void lilac_gpu_fault_load(void), lilac_gpu_fault_return(void);
__asm__(".text\n"
    ".globl _lilac_gpu_read32, _lilac_gpu_fault_load, _lilac_gpu_fault_return\n"
    "_lilac_gpu_read32:\n"
    "_lilac_gpu_fault_load: ldr w2, [x0]\n"
    "dsb sy\nstr w2, [x1]\nmov w0, #1\nret\n"
    "_lilac_gpu_fault_return: mov w0, #0\nret\n");

bool lilac_gpu_abort(arm_saved_state_t *state, uint32_t esr, vm_offset_t address)
{
    if (!lilac_gpu_smmu_matches(get_saved_state_pc(state), esr, address,
        get_saved_state_reg(state, 0), gpu_smmu, (uintptr_t)lilac_gpu_fault_load) &&
        !lilac_gpu_rpm_matches(get_saved_state_pc(state), esr, address,
        get_saved_state_reg(state, 0), gpu_rpm, (uintptr_t)lilac_gpu_fault_load)) return false;
    gpu_fault_esr = esr; gpu_fault_address = address;
    set_saved_state_pc(state, (uintptr_t)lilac_gpu_fault_return);
    return true;
}

/* Qualcomm's SMCCC capability calls take values only; no firmware is passed
 * or loaded and no secure state is changed by these queries. */
static int32_t gpu_tz_query(uint64_t convention, uint64_t function, uint64_t value, uint64_t *answer)
{
    register uint64_t x0 __asm__("x0") = convention | function;
    register uint64_t x1 __asm__("x1") = 1;
    register uint64_t x2 __asm__("x2") = value;
    register uint64_t x3 __asm__("x3") = 0;
    register uint64_t x4 __asm__("x4") = 0;
    register uint64_t x5 __asm__("x5") = 0;
    register uint64_t x6 __asm__("x6") = 0;
    register uint64_t x7 __asm__("x7") = 0;
    __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
        "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7) : :
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    *answer = x1;
    return (int32_t)x0;
}

static void gpu_tz_capabilities(void)
{
    const uint64_t sip = 2ull << 24, fast = 1ull << 31, smc64 = 1ull << 30;
    const uint64_t available = 0x601, init = 0x201, mem = 0x202, auth = 0x205, supported = 0x207;
    uint64_t convention = fast | sip | smc64;
    tz_result.attempted = 1;
    tz_result.probe64 = gpu_tz_query(convention, available, sip | available, &tz_result.probe64_value);
    if (tz_result.probe64 || tz_result.probe64_value != 1) {
        convention = fast | sip;
        tz_result.probe32 = gpu_tz_query(convention, available, sip | available, &tz_result.probe32_value);
        if (tz_result.probe32 || tz_result.probe32_value != 1) return;
        tz_result.convention = 32;
    } else tz_result.convention = 64;
    tz_result.init = gpu_tz_query(convention, available, sip | init, &tz_result.init_value);
    tz_result.mem_setup = gpu_tz_query(convention, available, sip | mem, &tz_result.mem_setup_value);
    tz_result.authenticate = gpu_tz_query(convention, available, sip | auth, &tz_result.authenticate_value);
    tz_result.supported = gpu_tz_query(convention, supported, 13, &tz_result.supported_value);
}

struct gpu_probe_io { vm_offset_t clock, gcc; };
static uint32_t cx_read(void *context, uint32_t offset)
{
    return *(volatile uint32_t *)(((struct gpu_probe_io *)context)->clock + offset);
}
static void cx_write(void *context, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(((struct gpu_probe_io *)context)->clock + offset) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
static void cx_delay(__unused void *context, uint32_t us)
{
    delay_for_interval(us, NSEC_PER_USEC);
}
/* Only the backend which retained CX may call this, after GX has collapsed. */
int lilac_a540_cx_release(void)
{
    if (!retained_cx) return 1;
    struct gpu_probe_io probe = {retained_cx,0};
    uint32_t current = cx_read(&probe,LILAC_A540_CX_GDSCR);
    cx_write(&probe,LILAC_A540_CX_GDSCR,current | LILAC_A540_CX_COLLAPSE);
    cx_delay(&probe,500);
    int ok = !!(cx_read(&probe,LILAC_A540_CX_GDSCR) & LILAC_A540_CX_COLLAPSE);
    printf("lilac A540 CX: retained vote released=%u status=%x\n",ok,cx_read(&probe,LILAC_A540_CX_HW_STATUS));

    return ok;
}
int lilac_a540_cx_acquire(void)
{
    if (!retained_cx) return 0;
    struct gpu_probe_io probe={retained_cx,0};
    uint32_t current=cx_read(&probe,LILAC_A540_CX_GDSCR);
    if (!(current & LILAC_A540_CX_COLLAPSE)) return 0;
    cx_write(&probe,LILAC_A540_CX_GDSCR,current & ~LILAC_A540_CX_COLLAPSE);
    for (unsigned i=0;i<1500;i++) {
        if (cx_read(&probe,LILAC_A540_CX_HW_STATUS)&LILAC_A540_CX_POWER_ON) { cx_delay(&probe,1); return 1; }
        cx_delay(&probe,1);
    }
    return 0;
}
/* The access guard covers only this function's load instruction and the
 * explicitly listed SMMU registers. Secure contexts 1 and 2 are not read. */
static void gpu_smmu_deep(void)
{
    static const uint16_t offsets[] = {0, 0x28, 0x3c, 0x48, 0x800, 0x804, 0x808,
        0xc00, 0xc04, 0xc08, 0x1000, 0x1004, 0x1008,
        0x8000, 0x8020, 0x8024, 0x8030, 0x8058};
    if (smmu_result.idr0 != 0x48016403 || smmu_result.idr1 != 0x20008003) {
        printf("lilac A540 SMMU detail: unexpected layout, no further reads\n");
        return;
    }
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        uint32_t value = 0;
        if (!lilac_gpu_read32(gpu_smmu + offsets[i], &value)) {
            printf("lilac A540 SMMU detail: fault at +%x ESR=%x\n", offsets[i], gpu_fault_esr);
            continue;
        }
        printf("lilac A540 SMMU detail: +%x=%x\n", offsets[i], value);
    }
}

/* Snapshot the RPM layout and queue indices only. We do not take over the
 * shared transport, reset its indices, send commands, or vote for a rail. */
static void gpu_rpm_snapshot(void)
{
    uint8_t table[LILAC_RPM_TOC_SIZE];
    struct lilac_rpm_layout layout;
    uint32_t index[4];
    gpu_rpm = ml_io_map(0x00778000, 0x7000);
    if (!gpu_rpm) { printf("lilac RPM: mapping failed\n"); return; }
    for (unsigned i = 0; i < sizeof(table); i += 4) {
        uint32_t value;
        if (!lilac_gpu_read32(gpu_rpm + 0x6f00 + i, &value)) {
            printf("lilac RPM: TOC read fault at +%x ESR=%x\n", 0x6f00 + i, gpu_fault_esr);
            return;
        }
        lilac_rpm_put32(table + i, value);
    }
    uint32_t entries = lilac_rpm_get32(table + 4);
    if (lilac_rpm_get32(table) == LILAC_RPM_TOC_ID && entries <= 20) {
        for (uint32_t i = 0; i < entries; i++) {
            const uint8_t *entry = table + 8 + 12 * i;
            printf("lilac RPM TOC: id=%x offset=%x size=%x\n", lilac_rpm_get32(entry),
                lilac_rpm_get32(entry + 4), lilac_rpm_get32(entry + 8));
        }
    }
    if (!lilac_rpm_layout_toc(table, 0x7000, &layout)) {
        printf("lilac RPM: unsupported TOC id=%x entries=%u; no channel access\n",
            lilac_rpm_get32(table), lilac_rpm_get32(table + 4));
        return;
    }
    const uint32_t offsets[] = {layout.tx.descriptor, layout.tx.descriptor + 4,
        layout.rx.descriptor, layout.rx.descriptor + 4};
    for (unsigned i = 0; i < 4; i++) {
        if (!lilac_gpu_read32(gpu_rpm + offsets[i], &index[i])) {
            printf("lilac RPM: descriptor fault at +%x ESR=%x\n", offsets[i], gpu_fault_esr);
            return;
        }
    }
    printf("lilac RPM: TX descriptor=%x size=%x read=%x write=%x; RX descriptor=%x size=%x read=%x write=%x\n",
        layout.tx.descriptor, layout.tx.size, index[0], index[1],
        layout.rx.descriptor, layout.rx.size, index[2], index[3]);
    for (unsigned i = 0; i < 4; i++) {
        uint32_t again;
        if (!lilac_gpu_read32(gpu_rpm + offsets[i], &again)) {
            printf("lilac RPM: second descriptor read fault at +%x ESR=%x\n", offsets[i], gpu_fault_esr);
            return;
        }
        if (again != index[i]) printf("lilac RPM: descriptor +%x changed from %x to %x\n", offsets[i], index[i], again);
    }
    if (index[0] >= layout.tx.size || index[1] >= layout.tx.size ||
        index[2] >= layout.rx.size || index[3] >= layout.rx.size ||
        ((index[0] | index[1] | index[2] | index[3]) & 7))
        printf("lilac RPM: indices are invalid for GLINK; do not take ownership\n");
}
static int cx_powered(void *context)
{
    struct gpu_probe_io *io = context;
    if (!io->gcc) return 1;
    smmu_result.ahb = *(volatile uint32_t *)(io->gcc + 4);
    /* Preserve the firmware's critical GPU AHB vote. Do not access the
     * SMMU unless that clock is enabled and reports running. */
    if (!(smmu_result.ahb & 1) || (smmu_result.ahb & 0x80000000u) || !gpu_smmu) return 1;
    smmu_result.attempted = 1;
    gpu_fault_esr = 0;
    gpu_fault_address = 0;
    if (lilac_gpu_read32(gpu_smmu + 0x20, &smmu_result.idr0) &&
        lilac_gpu_read32(gpu_smmu + 0x24, &smmu_result.idr1)) smmu_result.readable = 1;
    if (gpu_deep && smmu_result.readable) gpu_smmu_deep();
    int release = smmu_result.readable ? lilac_a540_probe() : 1;
    smmu_result.esr = gpu_fault_esr;
    if (gpu_fault_esr) smmu_result.fault_offset = (uint32_t)(gpu_fault_address - gpu_smmu);
    return release;
}

void lilac_gpu_snapshot_init(void)
{
    uint32_t enabled = 0;
    if (!PE_parse_boot_argn("lilac_gpu_snapshot", &enabled, sizeof(enabled)) || enabled != 1) return;
    vm_offset_t clock = ml_io_map(0x05065000, 0x2000);
    if (!clock) return;
    gpu_snapshot.cx_gdsc = *(volatile uint32_t *)(clock + 0x1004);
    gpu_snapshot.gx_gdsc = *(volatile uint32_t *)(clock + 0x1094);
    gpu_snapshot.core_clock = *(volatile uint32_t *)(clock + 0x1098);
    gpu_snapshot.timer_clock = *(volatile uint32_t *)(clock + 0x10d0);
    gpu_snapshot.isense_clock = *(volatile uint32_t *)(clock + 0x1124);
    gpu_snapshot.clamp = *(volatile uint32_t *)(clock + 0x130);
    __asm__ volatile("dsb sy" ::: "memory");
    gpu_snapshot.available = 1;
    printf("lilac GPU snapshot: CX=%x GX=%x core=%x timer=%x clamp=%x; no GPU commands\n",
        gpu_snapshot.cx_gdsc, gpu_snapshot.gx_gdsc, gpu_snapshot.core_clock,
        gpu_snapshot.timer_clock, gpu_snapshot.clamp);
    uint32_t cycle = 0;
    if (PE_parse_boot_argn("lilac_gpu_cx", &cycle, sizeof(cycle)) && cycle == 1) {
        if (!PE_parse_boot_argn("lilac_gpu_deep", &gpu_deep, sizeof(gpu_deep)) || gpu_deep != 1) gpu_deep = 0;
        if (gpu_deep) gpu_rpm_snapshot();
        uint32_t rpm_cycle = 0;
        if (PE_parse_boot_argn("lilac_gpu_rpm", &rpm_cycle, sizeof(rpm_cycle)) && rpm_cycle == 1) {
            printf("lilac A540 RPM: connecting verified version-0 rwmx resource\n");
            if (lilac_rpm_connect(true)) {
                printf("lilac A540 RPM: connected, requesting MX level 80\n");
                if (lilac_rpm_gpu_vote(0x80)) {
                    printf("lilac A540 RPM: MX level 80 acknowledged\n");
                    printf("lilac A540 RPM: release acknowledged=%u\n", lilac_rpm_gpu_vote(0));
                }
            }
        }
        struct gpu_probe_io probe = {clock, ml_io_map(0x00171000, 0x1000)};
        gpu_smmu = ml_io_map(0x05040000, 0x10000);
        const struct lilac_a540_cx_io io = {&probe, cx_read, cx_write, cx_delay, cx_powered};
        printf("lilac A540 CX: bounded vote; guarded SMMU ID reads only if GPU AHB is running\n");
        lilac_a540_cx_cycle(&io, &cx_result);
        if (cx_result.result == LILAC_CX_RETAINED) retained_cx = clock;
        printf("lilac A540 CX: result=%u before=%x hw=%x voted=%x on=%x restored=%x after=%x\n",
            cx_result.result, cx_result.before, cx_result.status_before, cx_result.voted,
            cx_result.status_on, cx_result.restored, cx_result.status_after);
        printf("lilac A540 SMMU: AHB=%x attempted=%u readable=%u IDR0=%x IDR1=%x ESR=%x offset=%x\n",
            smmu_result.ahb, smmu_result.attempted, smmu_result.readable, smmu_result.idr0,
            smmu_result.idr1, smmu_result.esr, smmu_result.fault_offset);
        printf("lilac A540 TZ: querying secure monitor capabilities\n");
        gpu_tz_capabilities();
        printf("lilac A540 TZ: ABI=%u probe64=%d/%llu probe32=%d/%llu init=%d/%llu mem=%d/%llu auth=%d/%llu supported=%d/%llu\n",
            tz_result.convention, tz_result.probe64, tz_result.probe64_value,
            tz_result.probe32, tz_result.probe32_value, tz_result.init, tz_result.init_value,
            tz_result.mem_setup, tz_result.mem_setup_value, tz_result.authenticate, tz_result.authenticate_value,
            tz_result.supported, tz_result.supported_value);
    }
}
