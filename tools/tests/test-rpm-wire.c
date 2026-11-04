/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-rpm-wire.h"

int main(void)
{
    uint8_t ram[1024] = {0}, out[40], old[40];
    const size_t toc = sizeof(ram) - 256;
    lilac_rpm_put32(ram + toc, LILAC_RPM_TOC_ID);
    lilac_rpm_put32(ram + toc + 4, 2);
    lilac_rpm_put32(ram + toc + 8, LILAC_RPM_TX_ID);
    lilac_rpm_put32(ram + toc + 12, 0x20);
    lilac_rpm_put32(ram + toc + 16, 0x100);
    lilac_rpm_put32(ram + toc + 20, LILAC_RPM_RX_ID);
    lilac_rpm_put32(ram + toc + 24, 0x180);
    lilac_rpm_put32(ram + toc + 28, 0x100);
    struct lilac_rpm_layout layout;
    assert(lilac_rpm_layout(ram, sizeof(ram), &layout));
    assert(layout.tx.descriptor == 0x20 && layout.tx.data == 0x28 && layout.rx.data == 0x188);
    assert(lilac_rpm_layout_toc(ram + toc, sizeof(ram), &layout));
    assert(!lilac_rpm_layout_toc(ram + toc, 255, &layout));
    lilac_rpm_put32(ram + toc + 24, 0x120); /* overlaps */
    assert(!lilac_rpm_layout(ram, sizeof(ram), &layout));
    lilac_rpm_put32(ram + toc + 24, 0xfffffff8u);
    assert(!lilac_rpm_layout(ram, sizeof(ram), &layout));
    lilac_rpm_put32(ram + toc + 24, 0x180);
    lilac_rpm_put32(ram + toc + 20, LILAC_RPM_TX_ID);
    assert(!lilac_rpm_layout(ram, sizeof(ram), &layout));
    lilac_rpm_put32(ram + toc + 4, 21);
    assert(!lilac_rpm_layout(ram, sizeof(ram), &layout));
    memset(out, 0xa5, sizeof(out)); memcpy(old, out, sizeof(out));
    assert(!lilac_rpm_rwmx_request(out, 27, 1, 2, 0, 16));
    assert(!lilac_rpm_rwmx_request(out, 40, 2, 2, 0, 16));
    assert(!lilac_rpm_rwmx_request(out, 40, 0, 1, 0, 16));
    assert(!memcmp(out, old, sizeof(out)));
    const uint32_t v0[] = {0x00716572, 32, 42, 0, 0x786d7772, 0, 12, 0x6c766c76, 4, 0x100};
    const uint32_t v1[] = {0x01000018, 42, 0x786d7772, 12, 0x6c766c76, 4, 0x100};
    assert(lilac_rpm_rwmx_request(out, sizeof(out), 0, 42, 0, 0x100) == sizeof(v0));
    for (size_t i = 0; i < sizeof(v0)/sizeof(*v0); i++) assert(lilac_rpm_get32(out + 4*i) == v0[i]);
    assert(lilac_rpm_rwmx_request(out, sizeof(out), 1, 42, 0, 0x100) == sizeof(v1));
    for (size_t i = 0; i < sizeof(v1)/sizeof(*v1); i++) assert(lilac_rpm_get32(out + 4*i) == v1[i]);
    puts("PASS: RPM layout bounds, overlap, duplicate IDs and rwmx wire formats");
}
