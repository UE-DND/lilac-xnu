#!/usr/bin/env python3
"""Execute the production SMP/loader return functions against a firmware model."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
psci = (ROOT / 'sources/xnu/osfmk/arm64/psci_smp.c').read_text()
loader = (ROOT / 'sources/xnu/osfmk/arm64/loader_return.c').read_text()


def function(source, name):
    start = source.index('\n' + name + '(') + 1
    start = source.rfind('\n', 0, start - 1) + 1  # return type
    if source[start - 7:start] == 'static\n':
        start -= 7
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define MAX_CPUS 8
#define GENERIC_PSCI_SMP 1
#define PSCI_CPU_OFF 0x84000002u
#define PSCI_AFFINITY_INFO 0xc4000004u
#define PSCI_AFFINITY_OFF 1
#define PSCI_SMP_CPU_OFF 4
#define PSCI_SMP_CPU_PARKED 5
#define PSCI_SMP_STOP_SECONDS 3
#define NSEC_PER_SEC 1
#define DSB_SY 0
#define DAIFSC_ALL 0xf
#define CNTV_CTL_EL0_IMASKED 2
#define __builtin_arm_wsr(r, v) ((void)(v))
#define __builtin_arm_wsr64(r, v) ((void)(v))
#define __builtin_arm_dsb(v) ((void)(v))
#define __builtin_arm_rsr64(r) ((uint64_t)topology.cpus[me].phys_id)
#define os_atomic_load(p, order) (*(p))
#define os_atomic_store(p, v, order) (*(p) = (v))
static bool exchange(int *p, int old, int value) {
    if (*p != old) return false;
    *p = value; return true;
}
#define os_atomic_cmpxchg(p, old, v, order) exchange(p, old, v)
typedef struct { uint32_t cpu_phys_id; } cpu_data_t;
static cpu_data_t cpus[8];
static struct { struct { uint32_t phys_id; } cpus[8]; } topology;
static uint32_t psci_smp_cpu_stage[8], psci_smp_started;
static uint32_t psci_smp_return_checked, psci_smp_return_phys[8];
static int64_t psci_smp_return_state[8], firmware[8];
static uint64_t psci_smp_return_mpidr, ticks;
static int master_cpu, me, psci_smp_stop_owner;
static bool psci_smp_boot_wanted, psci_smp_boot_busy, psci_smp_stopping;
static bool psci_smp_park_complete, psci_smp_poweroff, loader_return_entered;
static int fail_park, fail_off, callbacks, jumped, refused_cpu, wakes, off_calls;
static bool refused_parking;
static jmp_buf escape;
static char log_text[8192];
static unsigned log_size;
static int cpu_number(void) { return me; }
static int ml_get_max_cpu_number(void) { return 7; }
static cpu_data_t *cpu_datap(int n) { return &cpus[n]; }
#define ml_get_topology_info() (&topology)
static uint64_t mach_absolute_time(void) { return ++ticks; }
static void nanoseconds_to_absolutetime(uint64_t n, uint64_t *out) { *out = n; }
static void loader_log_putc(char c) {
    assert(log_size + 1 < sizeof(log_text)); log_text[log_size++] = c;
}
static void loader_smp_stop_refused(int cpu, uint32_t phys, int64_t state, bool parking) {
    assert(phys == topology.cpus[cpu].phys_id); (void)state;
    refused_cpu = cpu; refused_parking = parking; longjmp(escape, 1);
}
static void __builtin_arm_wfi(void) { longjmp(escape, 3); }
static void __builtin_arm_wfe(void) {
    /* Simulate the boot CPU doing cleanup while a secondary stays parked. */
    assert(me != master_cpu && psci_smp_cpu_stage[me] == PSCI_SMP_CPU_PARKED);
    assert(!psci_smp_poweroff); callbacks = 10; wakes++; psci_smp_poweroff = true;
}
static void __builtin_arm_sev(void) {
    if (!psci_smp_poweroff) return;
    assert(callbacks == 10);  /* no CPU_OFF before all device callbacks */
    for (int i = 1; i < 8; i++) {
        if (i != fail_off && psci_smp_cpu_stage[i] == PSCI_SMP_CPU_PARKED) {
            firmware[i] = 1; psci_smp_cpu_stage[i] = PSCI_SMP_CPU_OFF;
        }
    }
}
static void gic_v3_irq_signal_stop(uint32_t phys) {
    for (int i = 0; i < 8; i++) if (topology.cpus[i].phys_id == phys) {
        if (i == master_cpu && me != master_cpu) { psci_smp_stopping = true; return; }
        assert(psci_smp_stopping && !psci_smp_poweroff);
        if (i != fail_park && firmware[i] == 0) psci_smp_cpu_stage[i] = PSCI_SMP_CPU_PARKED;
        return;
    }
    assert(!"unknown CPU");
}
static void gic_v3_irq_cpu_shutdown(void) { assert(psci_smp_poweroff); }
static int64_t psci_call(uint64_t fn, uint64_t phys, uint64_t arg2, uint64_t arg3) {
    assert(arg2 == 0 && arg3 == 0);
    if (fn == PSCI_CPU_OFF) {
        assert(psci_smp_poweroff && callbacks == 10 && wakes == 1);
        off_calls++; firmware[me] = 1; longjmp(escape, 2);
    }
    assert(fn == PSCI_AFFINITY_INFO);
    for (int i = 0; i < 8; i++) if (topology.cpus[i].phys_id == phys) return firmware[i];
    assert(!"bad affinity"); return -1;
}
void loader_return(void);
void psci_smp_log_return_state(void);
static void psci_smp_park_self(void);
'''
for name in ('psci_smp_cpu_off', 'psci_smp_park_self', 'psci_smp_note', 'psci_smp_hex',
             'psci_smp_log_return_state', 'psci_smp_stop_rest', 'psci_smp_quiesce_others',
             'psci_smp_stop_others', 'psci_smp_stop_here'):
    program += function(psci, name)
program += r'''
static void device_callback(void) {
    assert(me == master_cpu && psci_smp_park_complete && !psci_smp_poweroff);
    for (int i = 1; i < 8; i++)
        assert(psci_smp_cpu_stage[i] == PSCI_SMP_CPU_PARKED || firmware[i] == 1);
    callbacks++;
}
#define hakios_ufs_quiesce device_callback
#define hakios_adreno_quiesce device_callback
#define hakios_wifi_quiesce device_callback
#define hakios_adsp_quiesce device_callback
#define hakios_mpss_quiesce device_callback
#define hakios_ddr_quiesce device_callback
#define hakios_haptic_quiesce device_callback
#define hakios_torch_quiesce device_callback
#define hakios_camera_quiesce device_callback
#define hakios_mic_quiesce device_callback
static void loader_log_linearize(void) { assert(psci_smp_poweroff); }
static void loader_log_append_msgbuf(void) {}
static void loader_log_append_panic_text(void) {}
static void lilac_fb_dump_panel(const char *s, uint64_t a, uint64_t b, uint64_t c, uint64_t d, bool r) {
    (void)s; (void)a; (void)b; (void)c; (void)d; (void)r;
}
static void loader_jump(uint64_t reason, uint64_t code) {
    assert(reason == 0 && code == 0 && callbacks == 10);
    assert(psci_smp_return_checked == 0xfe && psci_smp_poweroff);
    for (int i = 1; i < 8; i++) assert(firmware[i] == 1);
    jumped++;
}
'''
program += function(loader, 'loader_return')
program += r'''
static void setup(void) {
    memset(psci_smp_cpu_stage, 0, sizeof(psci_smp_cpu_stage));
    memset(firmware, 0, sizeof(firmware));
    memset(log_text, 0, sizeof(log_text)); log_size = 0;
    for (int i = 0; i < 8; i++) {
        topology.cpus[i].phys_id = (i / 4) * 0x100 + (i % 4);
        cpus[i].cpu_phys_id = topology.cpus[i].phys_id;
    }
    psci_smp_started = 0xfe; psci_smp_return_checked = 0;
    psci_smp_stop_owner = -1; master_cpu = me = 0;
    psci_smp_boot_wanted = psci_smp_boot_busy = psci_smp_stopping = false;
    psci_smp_park_complete = psci_smp_poweroff = loader_return_entered = false;
    ticks = 0; callbacks = jumped = wakes = off_calls = 0;
    fail_park = fail_off = refused_cpu = -1; refused_parking = false;
}
int main(void) {
    setup(); loader_return(); assert(jumped == 1);
    assert(strstr(log_text, "--- SMP return snapshot ---"));
    assert(strstr(log_text, "cpu 7 MPIDR=0x0000000000000103 PSCI=0x0000000000000001"));
    setup(); fail_park = 7;
    if (!setjmp(escape)) { loader_return(); assert(!"park failure returned"); }
    assert(refused_cpu == 7 && refused_parking && !callbacks && !jumped && !psci_smp_poweroff);
    setup(); fail_off = 7;
    if (!setjmp(escape)) { loader_return(); assert(!"OFF failure returned"); }
    assert(refused_cpu == 7 && !refused_parking && callbacks == 10 && !jumped);
    setup(); firmware[7] = 1; psci_smp_started &= ~(1u << 7);
    loader_return(); assert(jumped == 1); /* never-started core still verified */
    for (int state = -1; state <= 2; state += 3) {
        setup(); firmware[7] = state;
        if (!setjmp(escape)) { loader_return(); assert(!"bad affinity returned"); }
        assert(refused_cpu == 7 && refused_parking && !callbacks && !jumped);
    }
    setup(); me = 7;
    if (!setjmp(escape)) { psci_smp_quiesce_others(); assert(!"secondary returned"); }
    assert(psci_smp_boot_wanted && off_calls == 1 && !jumped && wakes == 1);
    setup(); psci_smp_boot_wanted = true;
    /* loader_jump returns in this model; stop_here must then halt. */
    if (!setjmp(escape)) psci_smp_stop_here();
    assert(psci_smp_boot_busy && jumped == 1);
    puts("PASS: production park/device-cleanup/OFF ordering, both refusal phases, and secondary handover");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='lilac-psci-return-') as directory:
    p = Path(directory)
    (p / 'test.c').write_text(program)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
