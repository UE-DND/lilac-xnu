/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/lilac-a540-gpmu.h"
static void expect_reject(uint8_t *fw,size_t len,uint32_t *out,size_t cap) {
    size_t done=123;uint32_t before=out[0];
    assert(!lilac_a540_gpmu_stream(fw,len,out,cap,&done));assert(done==123 && out[0]==before);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>12);rewind(f);
    uint8_t *fw=malloc((size_t)len);assert(fw && fread(fw,1,(size_t)len,f)==(size_t)len);fclose(f);
    uint32_t out[8192];memset(out,0xa5,sizeof(out));size_t done=0;
    assert(lilac_a540_gpmu_stream(fw,(size_t)len,out,8192,&done));
    size_t words=lilac_read_le32(fw)-lilac_read_le32(fw+8)-2,start=lilac_read_le32(fw+8)+3,offset=0,pos=0;
    assert(words==2923 && done==2947);
    while(offset<words) {
        size_t n=words-offset<127?words-offset:127;uint32_t tmp[128],values[127];
        for(size_t i=0;i<n;i++)values[i]=lilac_read_le32(fw+4*(start+offset+i));
        struct lilac_pm4 b={tmp,128,0};assert(lilac_pm4_type4(&b,0x8800+(uint32_t)offset,values,n));
        assert(!memcmp(out+pos,tmp,(n+1)*4));pos+=n+1;offset+=n;
    }
    assert(pos==done);expect_reject(fw,(size_t)len,out,done-1);expect_reject(fw,(size_t)len-1,out,8192);
    fw[4]^=1;expect_reject(fw,(size_t)len,out,8192);fw[4]^=1;
    memset(fw+8,0xff,4);expect_reject(fw,(size_t)len,out,8192);
    free(fw);puts("PASS: original A540 GPMU fw2 -> 2947 PM4 words; reconstruction and malformed input rejection");
}
