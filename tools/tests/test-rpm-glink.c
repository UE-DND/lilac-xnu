/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-rpm-glink.h"
struct fake { uint8_t ram[0x7000]; unsigned doorbells; };
static int get(void *v, uint32_t off, uint32_t *out) { struct fake *f = v; assert(off+4 <= sizeof(f->ram)); *out = lilac_rpm_get32(f->ram+off); return 1; }
static int put(void *v, uint32_t off, uint32_t value) { struct fake *f = v; assert(off+4 <= sizeof(f->ram)); lilac_rpm_put32(f->ram+off,value); return 1; }
static void barrier(void *v) { (void)v; }
static int bell(void *v) { ((struct fake *)v)->doorbells++; return 1; }
static void remote_send(struct fake *f, const uint8_t *p, uint32_t n) {
    uint32_t w=lilac_rpm_get32(f->ram+0x904);
    for (uint32_t i=0;i<n;i++) f->ram[0x908+(w+i)%0x80]=p[i];
    lilac_rpm_put32(f->ram+0x904,(w+n)%0x80);
}
static void remote_expect(struct fake *f, const uint8_t *p, uint32_t n) {
    uint32_t r=lilac_rpm_get32(f->ram+0x200), w=lilac_rpm_get32(f->ram+0x204);
    assert((w+0x80-r)%0x80==n);
    for (uint32_t i=0;i<n;i++) assert(f->ram[0x208+(r+i)%0x80]==p[i]);
    lilac_rpm_put32(f->ram+0x200,w);
}
int main(void) {
    struct fake f={0}; struct lilac_rpm_io io={&f,get,put,barrier,bell};
    struct lilac_rpm_glink l={.tx={{0x200,0x208,0x80},0},.rx={{0x900,0x908,0x80},0},.io=&io};
    uint8_t msg[24]={0}, reply[24]={0};
    assert(lilac_glink_step(&l)==1);
    lilac_glink_header(reply,0,1,0); remote_expect(&f,reply,8);
    lilac_glink_header(msg,0,1,4); remote_send(&f,msg,8);
    assert(lilac_glink_step(&l)==1 && l.peer_version);
    lilac_glink_header(reply,1,1,0); remote_expect(&f,reply,8);
    lilac_glink_header(msg,1,1,0); remote_send(&f,msg,8);
    assert(lilac_glink_step(&l)==1 && l.version_ack);
    assert(lilac_glink_step(&l)==1 && l.sent_open);
    memset(reply,0,sizeof(reply)); lilac_glink_header(reply,2,1,13); memcpy(reply+8,"rpm_requests",13); remote_expect(&f,reply,24);
    memset(msg,0,sizeof(msg)); lilac_glink_header(msg,2,7,13); memcpy(msg+8,"rpm_requests",13); remote_send(&f,msg,24);
    assert(lilac_glink_step(&l)==1 && l.remote_channel==7 && !lilac_glink_ready(&l));
    lilac_glink_header(reply,4,7,0); remote_expect(&f,reply,8);
    /* Real RPM offers additional services before acknowledging our OPEN. */
    memset(msg,0,sizeof(msg)); lilac_glink_header(msg,2,3,10); memcpy(msg+8,"rpm_other",10);
    remote_send(&f,msg,8);
    uint32_t before_open=lilac_rpm_get32(f.ram+0x900);
    assert(lilac_glink_step(&l)==0 && lilac_rpm_get32(f.ram+0x900)==before_open);
    remote_send(&f,msg+8,16);
    assert(lilac_glink_step(&l)==1 && l.remote_channel==7 && !lilac_glink_ready(&l));
    remote_expect(&f,reply,0);
    lilac_glink_header(msg,4,1,0); remote_send(&f,msg,8);
    assert(lilac_glink_step(&l)==1 && lilac_glink_ready(&l));
    assert(lilac_glink_rwmx_send(&l, 42, 0, 16)==1 && l.pending_id==42);
    uint8_t vote[56]={0}; lilac_glink_header(vote,9,1,0); lilac_rpm_put32(vote+8,40);
    assert(lilac_rpm_rwmx_request(vote+16,40,0,42,0,16)==40); remote_expect(&f,vote,56);
    assert(lilac_glink_rwmx_send(&l,43,0,16)==-1 && lilac_glink_rwmx_ack(&l)==0);
    uint8_t ack[40]={0}; lilac_glink_header(ack,9,7,0); lilac_rpm_put32(ack+8,20);
    lilac_rpm_put32(ack+16,0x00716572); lilac_rpm_put32(ack+20,12);
    lilac_rpm_put32(ack+24,0x2367736d); lilac_rpm_put32(ack+28,4); lilac_rpm_put32(ack+32,42);
    remote_send(&f,ack,16);
    assert(lilac_glink_step(&l)==0 && lilac_glink_rwmx_ack(&l)==0);
    remote_send(&f,ack+16,24);
    assert(lilac_glink_rwmx_ack(&l)==1 && l.pending_id==0);
    assert(lilac_glink_rwmx_send(&l, 43, 0, 16)==1);
    lilac_rpm_put32(vote+24,43); remote_expect(&f,vote,56);
    uint8_t nack[48]={0}; memcpy(nack,ack,36);
    lilac_rpm_put32(nack+8,32); lilac_rpm_put32(nack+20,24); lilac_rpm_put32(nack+32,43);
    lilac_rpm_put32(nack+36,0x00727265); lilac_rpm_put32(nack+40,4); memcpy(nack+44,"bad",4);
    remote_send(&f,nack,48);
    assert(lilac_glink_rwmx_ack(&l)==2 && !l.pending_id);
    assert(lilac_glink_rwmx_send(&l, 44, 0, 16)==1);
    lilac_rpm_put32(vote+24,44); remote_expect(&f,vote,56);
    lilac_rpm_put32(ack+32,99); remote_send(&f,ack,40);
    uint32_t unread=lilac_rpm_get32(f.ram+0x900);
    assert(lilac_glink_rwmx_ack(&l)==-1 && l.broken && lilac_rpm_get32(f.ram+0x900)==unread);
    struct fake bad={0}; struct lilac_rpm_io badio={&bad,get,put,barrier,bell};
    struct lilac_rpm_glink broken={.tx={{0x200,0x208,0x80},0},.rx={{0x900,0x908,0x80},0},.io=&badio,.sent_version=1};
    memset(msg,0,sizeof(msg)); lilac_glink_header(msg,2,7,13); memcpy(msg+8,"wrong_channel",13); remote_send(&bad,msg,24);
    assert(lilac_glink_step(&broken)==-1 && broken.broken);
    assert(lilac_rpm_get32(bad.ram+0x900)==0 && lilac_rpm_get32(bad.ram+0x204)==0);
    puts("PASS: RPM GLINK handshake with concurrent services, data ownership and malformed names");
}
