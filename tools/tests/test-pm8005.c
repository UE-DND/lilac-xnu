/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-pm8005.h"
struct fake {
    uint8_t regs[256];
    uint32_t write_data, read_data, status, owner, version, writes, reject;
};
static int read32(void *context, uint64_t address, uint32_t *value)
{
    struct fake *f = context;
    if (address == 0x0800f000) *value = f->version;
    else if (address >= 0x0800f800 && address < 0x08010000) *value = address == 0x0800f82c ? 0x51400 : 0;
    else if (address == 0x0800a72c) *value = f->owner;
    else if (address == 0x08458008 || address == 0x09458008) *value = f->status;
    else if (address == 0x09458018) *value = f->read_data;
    else { assert(0); return 0; }
    return 1;
}
static int write32(void *context, uint64_t address, uint32_t value)
{
    struct fake *f = context;
    if (address == 0x08458010) { f->write_data = value; return 1; }
    assert(address == 0x08458000 || address == 0x09458000);
    unsigned offset = (value >> 4) & 255, bytes = (value & 7) + 1;
    assert(bytes <= 2);
    f->status = 1;
    if (address == 0x09458000) {
        assert((value >> 27) == 1);
        f->read_data = f->regs[offset] | (bytes == 2 ? f->regs[offset+1] << 8 : 0);
    } else {
        assert((value >> 27) == 0);
        if (++f->writes == f->reject) { f->status = 5; return 1; }
        f->regs[offset] = f->write_data;
        if (bytes == 2) f->regs[offset+1] = f->write_data >> 8;
        if (offset == 0x46) f->regs[8] = f->regs[0x46] & 0x80;
    }
    return 1;
}
static void delay(void *context, uint32_t us) { (void)context; (void)us; }
static struct fake initial(void)
{
    struct fake f = {.version=0x30000000};
    f.regs[4]=0x1c; f.regs[5]=0xa; f.regs[0x40]=0x58; f.regs[0x41]=2; f.regs[0x45]=6;
    return f;
}
int main(void)
{
    struct fake f=initial(); struct lilac_pm8005 p;
    const struct lilac_pm8005_io io={&f,read32,write32,delay};
    assert(lilac_pm8005_snapshot(&io,&p) && p.millivolts==600 && p.channel==11 && !f.writes);
    assert(!lilac_pm8005_on(&io,&p,1088) && !f.writes);
    assert(lilac_pm8005_on(&io,&p,724) && f.regs[8]==0x80);
    assert(lilac_pm8005_restore(&io,&p) && !p.touched);
    struct fake original=initial(); assert(!memcmp(f.regs,original.regs,sizeof(f.regs)));
    for (unsigned failure=1;failure<=3;failure++) {
        f=initial(); f.reject=failure;
        assert(lilac_pm8005_snapshot(&io,&p));
        assert(!lilac_pm8005_on(&io,&p,716));
        assert(lilac_pm8005_restore(&io,&p));
        assert(!memcmp(f.regs,original.regs,sizeof(f.regs)));
    }
    f=initial(); f.owner=1; assert(!lilac_pm8005_snapshot(&io,&p) && !f.writes);
    f=initial(); f.regs[5]=9; assert(!lilac_pm8005_snapshot(&io,&p) && !f.writes);
    f=initial(); f.regs[0x46]=0x80;
    assert(lilac_pm8005_snapshot(&io,&p) && !lilac_pm8005_on(&io,&p,716) && !f.writes);
    puts("PASS: PM8005 ownership, subtype, voltage limits, SPMI encoding and failed enable restoration");
}
