/* SPDX-License-Identifier: GPL-2.0-only
 * RMI discovery and F12 frame acquisition, independent of the I2C transport.
 * Derived from Sony clearpad_core.c (LA.UM.6.4.r1). No controller writes,
 * interrupt acknowledgement, power sequencing or OS input injection here.
 */
#ifndef LILAC_CLEARPAD_RMI_H
#define LILAC_CLEARPAD_RMI_H
#include "lilac-clearpad-f12.h"

/* The transport must serialize page selection and complete each read, return
 * 0 on success, and reject requests crossing the RMI page-select byte. */
typedef int (*lilac_rmi_read)(void *, uint16_t, uint8_t *, size_t);
struct lilac_rmi_function { uint8_t page, query, command, control, data, irq_mask; };
struct lilac_rmi_device {
    struct lilac_rmi_function f01, f12;
    uint16_t objects, attention;
    struct lilac_f12_state fingers;
};

static inline int lilac_rmi_configured(uint8_t control, uint8_t requested, uint8_t status)
{
    return !(control & 3) && (control & 0x7f) == (requested & 0x7f) &&
        !(status & 0xc0) && (status & 15) <= 1;
}

static inline int lilac_rmi_read_at(lilac_rmi_read read, void *ctx, uint8_t page,
    unsigned addr, uint8_t *buf, size_t n)
{
    if (!read || !buf || !n || addr >= 255 || n > 255 - addr) return 0;
    return read(ctx, (uint16_t)((unsigned)page << 8 | addr), buf, n) == 0;
}

/* Query 4/7: length, then a one- or three-byte structure size, then presence. */
static inline int lilac_rmi_presence(lilac_rmi_read read, void *ctx,
    const struct lilac_rmi_function *f, unsigned query, uint8_t offsets[32])
{
    uint8_t n, first, bytes[35], tmp[32];
    if (!lilac_rmi_read_at(read,ctx,f->page,(unsigned)f->query+query,&n,1) || !n || n>32 ||
        !lilac_rmi_read_at(read,ctx,f->page,(unsigned)f->query+query+1,&first,1)) return 0;
    size_t header = first ? 1 : 3;
    if (!lilac_rmi_read_at(read,ctx,f->page,(unsigned)f->query+query+1,bytes,header+n)) return 0;
    for (size_t i=0;i<32;i++) tmp[i]=0xff;
    size_t count = n < 4 ? (size_t)n*8 : 32;
    if (!lilac_f12_offsets(bytes+header,n,tmp,count)) return 0;
    for (size_t i=0;i<32;i++) offsets[i]=tmp[i];
    return 1;
}

/* Sony scans five PDT pages. Refuse ambiguous descriptors or interrupt masks
 * outside the single status byte supported by this initial integration. */
static inline int lilac_rmi_discover(lilac_rmi_read read, void *ctx, struct lilac_rmi_device *out)
{
    struct lilac_rmi_device d = {0}; unsigned irq=0; int found1=0, found12=0;
    uint8_t fd[6], q, ctrl[32], data[32], b[4];
    if (!read || !out) return 0;
    for (unsigned page=0;page<5;page++) {
        int terminated=0;
        for (int a=0xe9;a>=0x05;a-=6) {
            if (!lilac_rmi_read_at(read,ctx,(uint8_t)page,(unsigned)a,fd,6)) return 0;
            if (!fd[5]) { terminated=1; break; }
            unsigned n=fd[4]&7u;
            if (irq+n>8) return 0;
            struct lilac_rmi_function f={(uint8_t)page,fd[0],fd[1],fd[2],fd[3],(uint8_t)(((1u<<n)-1u)<<irq)};
            irq+=n;
            if (fd[5]==1) { if (found1++) return 0; d.f01=f; }
            if (fd[5]==0x12) { if (found12++) return 0; d.f12=f; }
        }
        if (!terminated) return 0;
    }
    if (!found1 || !found12 || !d.f12.irq_mask ||
        !lilac_rmi_read_at(read,ctx,d.f12.page,d.f12.query,&q,1) || !(q&1) ||
        !lilac_rmi_presence(read,ctx,&d.f12,4,ctrl) ||
        !lilac_rmi_presence(read,ctx,&d.f12,7,data) ||
        ctrl[8]==0xff || ctrl[23]==0xff || ctrl[28]==0xff || data[1]==0xff || data[15]==0xff) return 0;
    if (!lilac_rmi_read_at(read,ctx,d.f12.page,(unsigned)d.f12.control+ctrl[8],b,4) ||
        ((unsigned)b[0]|(unsigned)b[1]<<8)!=719 || ((unsigned)b[2]|(unsigned)b[3]<<8)!=1279 ||
        !lilac_rmi_read_at(read,ctx,d.f12.page,(unsigned)d.f12.control+ctrl[23],b,2) || b[1]!=10 ||
        !lilac_rmi_read_at(read,ctx,d.f12.page,(unsigned)d.f12.control+ctrl[28],b,1) || b[0]!=0xff) return 0;
    unsigned objects=(unsigned)d.f12.data+data[1], attention=(unsigned)d.f12.data+data[15];
    if (objects+LILAC_F12_SLOTS*LILAC_F12_BYTES>255 || attention+2>255) return 0;
    d.objects=(uint16_t)((unsigned)d.f12.page<<8|objects);
    d.attention=(uint16_t)((unsigned)d.f12.page<<8|attention);
    *out=d;
    return 1;
}

/* Attention can change while a frame is read. Reject changed masks; equal
 * masks still do not guarantee that moving coordinates were sampled atomically. */
static inline int lilac_rmi_poll(lilac_rmi_read read, void *ctx, struct lilac_rmi_device *d,
    struct lilac_f12_event *events, size_t capacity, size_t *event_count)
{
    uint8_t before[2], after[2], bytes[80]; size_t records=0;
    if (!read || !d || !events || !event_count || !d->objects || !d->attention ||
        read(ctx,d->attention,before,2)) return 0;
    uint16_t mask=(uint16_t)(before[0]|(uint16_t)before[1]<<8);
    if (mask & ~0x3ffu) return 0;
    for (uint16_t bits=mask;bits;bits>>=1) records++;
    if ((records && read(ctx,d->objects,bytes,records*8)) || read(ctx,d->attention,after,2) ||
        before[0]!=after[0] || before[1]!=after[1]) return 0;
    return lilac_f12_frame(&d->fingers,mask,records?bytes:NULL,records*8,events,capacity,event_count);
}
#endif
