/* SPDX-License-Identifier: GPL-2.0-only
 * MSM8998 RPM GLINK transport for the A540 backend. Entry points are inert
 * until a caller that owns this transport explicitly calls connect. The caller
 * must serialize all calls and validate the target device tree first.
 */
#include <mach/mach_types.h>
#include <kern/clock.h>
#include <arm/thread.h>
#include <sys/systm.h>
#include "../../../../kernel/lilac-rpm-api.h"
#include "../../../../kernel/lilac-rpm-glink.h"
#include "../../../../kernel/lilac-rpm-access.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern int lilac_rpm_load32(vm_offset_t, uint32_t *);
extern int lilac_rpm_store32(vm_offset_t, uint32_t);
extern void lilac_rpm_load_pc(void), lilac_rpm_store_pc(void), lilac_rpm_recover_pc(void);
__asm__(".text\n"
    ".globl _lilac_rpm_load32, _lilac_rpm_load_pc, _lilac_rpm_store32, _lilac_rpm_store_pc, _lilac_rpm_recover_pc\n"
    "_lilac_rpm_load32:\n_lilac_rpm_load_pc: ldr w2, [x0]\ndsb sy\nstr w2, [x1]\nmov w0, #1\nret\n"
    "_lilac_rpm_store32:\n_lilac_rpm_store_pc: str w1, [x0]\ndsb sy\nmov w0, #1\nret\n"
    "_lilac_rpm_recover_pc: mov w0, #0\nret\n");

static vm_offset_t rpm_ram, rpm_doorbell;
static struct lilac_rpm_layout rpm_layout;
static struct lilac_rpm_glink rpm_link;
static uint32_t rpm_message_id = 2;
static bool rpm_attempted, rpm_write_enabled, rpm_failed;
static uint32_t rpm_last_esr;
static vm_offset_t rpm_last_fault;

bool lilac_rpm_abort(arm_saved_state_t *state, uint32_t esr, vm_offset_t address)
{
    if (!lilac_rpm_fault_matches(get_saved_state_pc(state), esr, address, get_saved_state_reg(state, 0),
        (uintptr_t)lilac_rpm_load_pc, (uintptr_t)lilac_rpm_store_pc, rpm_ram, rpm_doorbell,
        rpm_write_enabled, &rpm_layout)) return false;
    rpm_last_esr = esr; rpm_last_fault = address;
    set_saved_state_pc(state, (uintptr_t)lilac_rpm_recover_pc);
    return true;
}

static int rpm_read(__unused void *context, uint32_t offset, uint32_t *value)
{
    if (rpm_ram && offset <= 0x6ffc && !(offset & 3) && lilac_rpm_load32(rpm_ram + offset, value)) return 1;
    rpm_failed = true;
    return 0;
}
static int rpm_write(__unused void *context, uint32_t offset, uint32_t value)
{
    if (rpm_write_enabled && lilac_rpm_write_offset(&rpm_layout, offset) &&
        lilac_rpm_store32(rpm_ram + offset, value)) return 1;
    rpm_failed = true;
    return 0;
}
static void rpm_barrier(__unused void *context) { __asm__ volatile("dsb sy" ::: "memory"); }
static int rpm_notify(__unused void *context)
{
    if (rpm_write_enabled && rpm_doorbell && lilac_rpm_store32(rpm_doorbell, 1)) return 1;
    rpm_failed = true;
    return 0;
}
static const struct lilac_rpm_io rpm_io = {NULL, rpm_read, rpm_write, rpm_barrier, rpm_notify};

/* A caller must validate the exact RPM resources and protocol version in the
 * target DTB before passing verified=true. A failed attempt is never replayed:
 * a published packet or vote may already have reached the remote processor. */
bool lilac_rpm_connect(bool verified)
{
    uint8_t table[LILAC_RPM_TOC_SIZE];
    uint32_t txr, txw, rxr, rxw;
    if (!verified) return false;
    if (rpm_attempted) return !rpm_failed && rpm_write_enabled && lilac_glink_ready(&rpm_link);
    rpm_attempted = true;
    rpm_ram = ml_io_map(0x00778000, 0x7000);
    vm_offset_t bell_page = ml_io_map(0x17911000, 0x1000);
    if (!rpm_ram || !bell_page) goto fail;
    rpm_doorbell = bell_page + 8;
    for (uint32_t i = 0; i < sizeof(table); i += 4) {
        uint32_t word;
        if (!rpm_read(NULL, 0x6f00 + i, &word)) goto fail;
        lilac_rpm_put32(table + i, word);
    }
    if (!lilac_rpm_layout_toc(table, 0x7000, &rpm_layout)) goto fail;
    rpm_link.tx.fifo = rpm_layout.tx; rpm_link.rx.fifo = rpm_layout.rx; rpm_link.io = &rpm_io;
    if (!lilac_rpm_indices(&rpm_link.tx, &rpm_io, &txr, &txw) ||
        !lilac_rpm_indices(&rpm_link.rx, &rpm_io, &rxr, &rxw) || txr || txw || rxr || rxw) goto fail;
    printf("lilac RPM: empty queues TX=%x/%x RX=%x/%x; begin GLINK v1\n",
        rpm_layout.tx.descriptor, rpm_layout.tx.size, rpm_layout.rx.descriptor, rpm_layout.rx.size);
    rpm_write_enabled = true;
    for (unsigned i = 0; i < 50000; i++) {
        if (lilac_glink_ready(&rpm_link)) return true;
        if (lilac_glink_step(&rpm_link) < 0 || rpm_failed) break;
        delay_for_interval(100, NSEC_PER_USEC);
    }
fail:
    printf("lilac RPM: version sent=%u peer=%u ack=%u open=%u ack=%u remote=%u broken=%u\n",
        rpm_link.sent_version, rpm_link.peer_version, rpm_link.version_ack, rpm_link.sent_open,
        rpm_link.open_ack, rpm_link.remote_channel, rpm_link.broken);
    if (rpm_ram && rpm_layout.tx.size && rpm_layout.rx.size &&
        lilac_rpm_indices(&rpm_link.tx, &rpm_io, &txr, &txw) &&
        lilac_rpm_indices(&rpm_link.rx, &rpm_io, &rxr, &rxw)) {
        printf("lilac RPM: final TX read=%x write=%x RX read=%x write=%x\n", txr, txw, rxr, rxw);
        uint8_t peek[8];
        if (lilac_rpm_peek(&rpm_link.rx, &rpm_io, peek, sizeof(peek)))
            printf("lilac RPM: next RX header=%x/%x\n", lilac_rpm_get32(peek), lilac_rpm_get32(peek + 4));
    }
    rpm_failed = true;
    rpm_write_enabled = false;
    printf("lilac RPM: connect failed; ESR=%x address=%llx; no retry\n", rpm_last_esr, (uint64_t)rpm_last_fault);
    return false;
}

/* The supported levels are the exact MSM8998 GPU DT levels. A zero level
 * relinquishes our active-set vote. False after timeout means unknown result;
 * subsequent requests are disabled until the system is restarted. */
bool lilac_rpm_gpu_vote(uint32_t level)
{
    if (rpm_failed || !rpm_write_enabled || !lilac_glink_ready(&rpm_link) ||
        (level != 0 && level != 0x80 && level != 0x100 && level != 0x180)) return false;
    uint32_t id = rpm_message_id++;
    if (id < 2 || rpm_message_id < 2) { rpm_failed = true; return false; }
    for (unsigned i = 0; i < 50000; i++) {
        int result = lilac_glink_rwmx_send(&rpm_link, id, 0, level);
        if (result > 0) goto sent;
        if (result < 0 || rpm_failed) goto fail;
        delay_for_interval(100, NSEC_PER_USEC);
    }
    goto fail;
sent:
    for (unsigned i = 0; i < 50000; i++) {
        int progress = lilac_glink_step(&rpm_link);
        if (progress < 0 || rpm_failed) break;
        if (!progress) {
            int ack = lilac_glink_rwmx_ack(&rpm_link);
            if (ack == 1) return true;
            if (ack < 0 || ack == 2) break;
        }
        delay_for_interval(100, NSEC_PER_USEC);
    }
fail:
    rpm_failed = true;
    rpm_write_enabled = false;
    printf("lilac RPM: vote %x failed or timed out; state unknown, no retry\n", level);
    return false;
}
