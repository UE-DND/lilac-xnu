/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-rpm-fifo.h"
struct fake { uint8_t ram[0x7000]; unsigned step, data_last, barrier_at, publish_at, bell_at; int fail_write, fail_bell; };
static int read32(void *v, uint32_t off, uint32_t *value) {
    struct fake *f = v; assert(off <= sizeof(f->ram)-4); *value = lilac_rpm_get32(f->ram + off); return 1;
}
static int write32(void *v, uint32_t off, uint32_t value) {
    struct fake *f = v; assert(off <= sizeof(f->ram)-4); f->step++;
    if (f->fail_write && off == 0x204) return 0;
    lilac_rpm_put32(f->ram + off, value);
    if (off == 0x204 || off == 0x200) {
        assert(f->barrier_at > f->data_last);
        f->publish_at = f->step;
    } else f->data_last = f->step;
    return 1;
}
static void barrier(void *v) { struct fake *f = v; f->barrier_at = ++f->step; }
static int bell(void *v) { struct fake *f = v; f->bell_at = ++f->step; return !f->fail_bell; }
int main(void) {
    struct fake f = {0}; struct lilac_rpm_io io = {&f, read32, write32, barrier, bell};
    struct lilac_rpm_queue q = {{0x200, 0x208, 0x30}, 0};
    uint8_t packet[24], result[24];
    for (unsigned i=0;i<sizeof(packet);i++) packet[i] = (uint8_t)i;
    lilac_rpm_put32(f.ram+0x200, 0x20); lilac_rpm_put32(f.ram+0x204, 0x28);
    assert(lilac_rpm_send(&q, &io, packet, sizeof(packet)));
    assert(lilac_rpm_get32(f.ram+0x204)==0x10);
    assert(f.data_last < f.publish_at && f.publish_at < f.barrier_at && f.barrier_at < f.bell_at);
    memset(result, 0, sizeof(result));
    lilac_rpm_put32(f.ram+0x200, 0x28);
    assert(lilac_rpm_peek(&q, &io, result, sizeof(result)) && !memcmp(result, packet, sizeof(packet)));
    assert(!lilac_rpm_consume(&q, &io, 32));
    assert(lilac_rpm_consume(&q, &io, 24));
    assert(lilac_rpm_get32(f.ram+0x200)==0x10);
    uint8_t before[sizeof(f.ram)]; memcpy(before, f.ram, sizeof(before));
    lilac_rpm_put32(f.ram+0x200, 8); lilac_rpm_put32(f.ram+0x204, 0);
    memcpy(before, f.ram, sizeof(before));
    assert(!lilac_rpm_send(&q, &io, packet, 8));
    assert(!memcmp(before, f.ram, sizeof(before)));
    lilac_rpm_put32(f.ram+0x200, 0); lilac_rpm_put32(f.ram+0x204, 0);
    f.fail_write = 1;
    assert(!lilac_rpm_send(&q, &io, packet, 8) && q.poisoned);
    f.fail_write = 0;
    assert(!lilac_rpm_send(&q, &io, packet, 8));
    q.poisoned=0; f.fail_bell=1;
    assert(!lilac_rpm_send(&q, &io, packet, 8) && q.poisoned);
    puts("PASS: RPM FIFO wrap, capacity, publication ordering and uncertain failure");
}
