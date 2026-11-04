/* PM8998 PON and GPIO keys, read-only over the MSM8998 SPMI observer.
 * Register layouts: Sony kernel 696bf916 drivers/spmi/spmi-pmic-arb.c,
 * drivers/gpio/qpnp-pin.c and drivers/input/misc/qpnp-power-on.c. */
#include <kern/clock.h>
#include <kern/thread.h>
#include <mach/mach_types.h>
#include <arm/thread.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>
#include "../../../../kernel/lilac-button-state.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
extern int lilac_buttons_load(vm_offset_t, uint32_t *);
extern int lilac_buttons_store(vm_offset_t, uint32_t);
extern void lilac_buttons_load_pc(void), lilac_buttons_store_pc(void), lilac_buttons_recover(void);
__asm__(".text\n"
    ".globl _lilac_buttons_load, _lilac_buttons_load_pc, _lilac_buttons_store, _lilac_buttons_store_pc, _lilac_buttons_recover\n"
    "_lilac_buttons_load:\n_lilac_buttons_load_pc: ldr w2,[x0]\ndsb sy\nstr w2,[x1]\nmov w0,#1\nret\n"
    "_lilac_buttons_store:\n_lilac_buttons_store_pc: str w1,[x0]\ndsb sy\nmov w0,#1\nret\n"
    "_lilac_buttons_recover: mov w0,#0\nret\n");

static vm_offset_t pages[4];
static unsigned count;
static bool faulted;
bool lilac_buttons_abort(arm_saved_state_t *state, uint32_t esr, vm_offset_t address)
{
    uintptr_t pc = get_saved_state_pc(state);
    uintptr_t expected = (esr & 64) ? (uintptr_t)lilac_buttons_store_pc : (uintptr_t)lilac_buttons_load_pc;
    if (((esr >> 26) & 63) != 0x25 || pc != expected || address != get_saved_state_reg(state, 0) || (address & 3)) return false;
    for (unsigned i = 0; i < count; i++) if (address >= pages[i] && address - pages[i] < 0x1000) {
        faulted = true;
        set_saved_state_pc(state, (uintptr_t)lilac_buttons_recover);
        return true;
    }
    return false;
}
struct key { uint16_t ppid; uint32_t channel, record; vm_offset_t observer; struct lilac_button_state state; bool usable; };
static struct key keys[] = {
    {.ppid=0x008,.record=HAKIOS_BUTTON_LOCK},
    {.ppid=0x0c6,.record=HAKIOS_BUTTON_VOLUME_UP},
    {.ppid=0x0c5,.record=HAKIOS_BUTTON_VOLUME_DOWN}
};
static vm_offset_t core;
static bool read_reg(struct key *key, unsigned reg, uint8_t *value)
{
    uint32_t status, data;
    bool ok = false;
    hakios_spmi_lock();
    if (!lilac_buttons_store(key->observer, (1u << 27) | (reg << 4))) goto done;
    for (unsigned i = 0; i < 100; i++) {
        if (!lilac_buttons_load(key->observer + 8, &status)) goto done;
        if (status & 1) {
            if ((status & 14) || !lilac_buttons_load(key->observer + 0x18, &data)) goto done;
            *value = (uint8_t)data;
            ok = true;
            break;
        }
        delay_for_interval(1, NSEC_PER_USEC);
    }
done:
    hakios_spmi_unlock();
    return ok;
}
static bool identify(struct key *key, bool power)
{
    uint8_t type, subtype, rev, mode, status;
    if (!read_reg(key, 4, &type) || !read_reg(key, 5, &subtype)) return false;
    if (power) return type == 1 && subtype >= 1 && subtype <= 5;
    if (type != 0x10 || !(subtype == 1 || subtype == 5 || subtype == 9 || subtype == 13 || subtype == 16 || subtype == 17)) return false;
    if (!read_reg(key, 1, &rev) || !read_reg(key, 0x40, &mode) || !read_reg(key, 8, &status)) return false;
    if (subtype == 16 || subtype == 17) mode &= 3; else mode = (mode >> 4) & 7;
    return mode == 0 && (status & (rev ? 0x80 : 0x02));
}
static void buttons_worker(__unused void *arg, __unused wait_result_t result)
{
    uint32_t version, value;
    core = ml_io_map(0x0800f000, 0x1000);
    if (!core) goto out;
    pages[count++] = core;
    if (!lilac_buttons_load(core, &version) || version < 0x20010000 || version >= 0x50000000) goto out;
    for (unsigned k = 0; k < 3; k++) {
        struct key *key = &keys[k];
        for (unsigned ch = 0; ch < 511; ch++) {
            if (!lilac_buttons_load(core + 0x800 + ch * 4, &value)) goto out;
            if (((value >> 8) & 0xfff) != key->ppid) continue;
            key->channel = ch;
            key->observer = ml_io_map(0x09400000 + ch * 0x8000, 0x1000);
            if (!key->observer) goto out;
            pages[count++] = key->observer;
            key->usable = identify(key, k == 0);
            printf("lilac buttons: key=%u channel=%u usable=%u\n", key->record, ch, key->usable);
            break;
        }
    }
    if (faulted) goto out;
    for (;;) {
        for (unsigned k = 0; k < 3; k++) {
            struct key *key = &keys[k];
            uint8_t status = 0;
            bool ok = key->usable && read_reg(key, k == 0 ? 0x10 : 8, &status);
            int event = lilac_button_sample(&key->state, ok, k == 0 ? (status & 1) != 0 : !(status & 1));
            if (event) hakios_touch_post(event > 0 ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP, key->record, 0, 0);
            if (faulted) goto out;
        }
        delay_for_interval(16, NSEC_PER_MSEC);
    }
out:
    printf("lilac buttons: stopped (SPMI unavailable or MMIO fault)\n");
    thread_terminate(current_thread());
}
void lilac_buttons_init(void)
{
    uint32_t enabled = 0;
    thread_t thread;
    if (PE_parse_boot_argn("lilac_buttons", &enabled, sizeof(enabled)) && enabled == 1 &&
        kernel_thread_start(buttons_worker, NULL, &thread) == KERN_SUCCESS) thread_deallocate(thread);
}
