/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "../../kernel/lilac-rpm-access.h"
int main(void)
{
    struct lilac_rpm_layout layout = {{0x200,0x208,0x6f8},{0x900,0x908,0x6f8}};
    assert(lilac_rpm_write_offset(&layout,0x204));
    assert(lilac_rpm_write_offset(&layout,0x900));
    assert(lilac_rpm_write_offset(&layout,0x208));
    assert(lilac_rpm_write_offset(&layout,0x8fc));
    assert(!lilac_rpm_write_offset(&layout,0x200));
    assert(!lilac_rpm_write_offset(&layout,0x904));
    assert(!lilac_rpm_write_offset(&layout,0x908));
    assert(!lilac_rpm_write_offset(&layout,0x20a));
    assert(!lilac_rpm_write_offset(&layout,0x6f00));
    assert(!lilac_rpm_write_offset(NULL,0));
    uint64_t base=0xffff000010000000ull, bell=0xffff000020000008ull, load=0xffff000030000000ull, store=load+32;
    const uint32_t read_esr=0x94000010, write_esr=0x94000050;
    assert(lilac_rpm_fault_matches(load,read_esr,base+0x6ffc,base+0x6ffc,load,store,base,bell,0,&layout));
    assert(!lilac_rpm_fault_matches(load,read_esr,base+0x7000,base+0x7000,load,store,base,bell,0,&layout));
    assert(lilac_rpm_fault_matches(store,write_esr,base+0x204,base+0x204,load,store,base,bell,1,&layout));
    assert(lilac_rpm_fault_matches(store,write_esr,bell,bell,load,store,base,bell,1,&layout));
    assert(!lilac_rpm_fault_matches(store,write_esr,base+0x200,base+0x200,load,store,base,bell,1,&layout));
    assert(!lilac_rpm_fault_matches(store,write_esr,base+0x204,base+0x204,load,store,base,bell,0,&layout));
    assert(!lilac_rpm_fault_matches(store,write_esr,bell,bell+4,load,store,base,bell,1,&layout));
    assert(!lilac_rpm_fault_matches(store+4,write_esr,bell,bell,load,store,base,bell,1,&layout));
    puts("PASS: RPM writes limited to local FIFO ownership");
}
