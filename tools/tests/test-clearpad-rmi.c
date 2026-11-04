/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-clearpad-rmi.h"

struct fixture { int corrupt, fail_at, calls, changing; uint16_t attention; uint8_t object[80]; };
static int fake(void *arg, uint16_t address, uint8_t *buf, size_t n) {
    struct fixture *f=arg; f->calls++;
    if (f->calls==f->fail_at) return -1;
    memset(buf,0,n);
    unsigned page=address>>8, a=address&255;
    if (n==6 && a>=5 && a<=0xe9 && (0xe9-a)%6==0) {
        if (page==0 && a==0xe9) { uint8_t fd[]={0x70,0x71,0x72,0x73,1,1}; memcpy(buf,fd,6); }
        else if (page==0 && a==0xe3) { uint8_t fd[]={0x10,0x20,0x40,0x80,1,0x12}; memcpy(buf,fd,6); }
        return 0;
    }
    if (page) return -1;
    if (a==0x10 && n==1) { buf[0]=1; return 0; }
    if (a==0x14 && n==1) { buf[0]=4; return 0; }
    if (a==0x15 && n==1) { buf[0]=1; return 0; }
    if (a==0x15 && n==5) { uint8_t x[]={1,0,1,0x80,0x10}; memcpy(buf,x,5); return 0; }
    if (a==0x17 && n==1) { buf[0]=2; return 0; }
    if (a==0x18 && n==1) { buf[0]=1; return 0; }
    if (a==0x18 && n==3) { uint8_t x[]={1,2,0x80}; memcpy(buf,x,3); return 0; }
    if (a==0x40 && n==4) { uint8_t x[]={0xcf,2,0xff,4}; memcpy(buf,x,4); if(f->corrupt) buf[0]--; return 0; }
    if (a==0x41 && n==2) { buf[1]=10; return 0; }
    if (a==0x42 && n==1) { buf[0]=0xff; return 0; }
    if (a==0x81 && n==2) { uint16_t m=f->attention; if(f->changing++ == 1) m^=1; buf[0]=m; buf[1]=m>>8; return 0; }
    if (a==0x80 && n<=80) { memcpy(buf,f->object,n); return 0; }
    return -1;
}
int main(void) {
    assert(lilac_rmi_configured(0, 0x80, 0));
    assert(lilac_rmi_configured(0x80, 0x80, 0));
    assert(lilac_rmi_configured(4, 0x84, 1));
    assert(!lilac_rmi_configured(0, 0x80, 0x81));
    assert(!lilac_rmi_configured(0, 0x80, 0x40));
    assert(!lilac_rmi_configured(0, 0x80, 6));
    assert(!lilac_rmi_configured(1, 0x80, 0));
    assert(!lilac_rmi_configured(1, 0x81, 0));
    assert(!lilac_rmi_configured(0, 0x84, 0));
    struct fixture f={0}; struct lilac_rmi_device d={0}, original;
    struct lilac_f12_event e[10]; size_t n=77;
    assert(lilac_rmi_discover(fake,&f,&d));
    assert(d.f01.irq_mask==1 && d.f12.irq_mask==2 && d.objects==0x80 && d.attention==0x81);
    int calls=f.calls;
    for(int i=1;i<=calls;i++) { struct fixture bad={.fail_at=i}; original=d; assert(!lilac_rmi_discover(fake,&bad,&d)); assert(!memcmp(&d,&original,sizeof d)); }
    f.corrupt=1; assert(!lilac_rmi_discover(fake,&f,&d)); f.corrupt=0;
    f.attention=1; f.object[0]=1; f.object[1]=10; f.object[3]=20;
    f.changing=-10; assert(lilac_rmi_poll(fake,&f,&d,e,10,&n) && n==1 && e[0].kind==LILAC_TOUCH_DOWN);
    f.changing=0; original=d; n=77;
    assert(!lilac_rmi_poll(fake,&f,&d,e,10,&n) && n==77 && !memcmp(&d,&original,sizeof d));
    f.changing=-10; f.attention=0;
    assert(lilac_rmi_poll(fake,&f,&d,e,10,&n) && n==1 && e[0].kind==LILAC_TOUCH_UP);
    puts("PASS: RMI PDT/F12 discovery, bounded reads, changing-frame and transport failure handling");
}
