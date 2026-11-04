/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-a540-zap.h"
static void put(uint8_t *p, uint32_t v) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
int main(void)
{
    uint8_t mdt[148+1024]={0}, bad[sizeof(mdt)];
    put(mdt,0x464c457f); put(mdt+4,0x10101); put(mdt+16,0xa40002); put(mdt+20,1);
    put(mdt+28,52); put(mdt+40,0x200034); put(mdt+44,3); put(mdt+68,148);
    put(mdt+100,1024); put(mdt+108,0x02200000);
    put(mdt+116,1); put(mdt+128,0x5000); put(mdt+132,4900); put(mdt+136,4900);
    put(mdt+140,0x08000007); put(mdt+144,0x100000);
    struct lilac_a540_zap_layout result={0};
    assert(lilac_a540_zap_layout(mdt,sizeof(mdt),4900,&result));
    assert(result.memory_bytes==8192 && result.file_bytes==4900 && result.alignment==0x100000);
    for (size_t n=0;n<sizeof(mdt);n++) assert(!lilac_a540_zap_layout(mdt,n,4900,&result));
    const struct { unsigned offset; uint32_t value; } invalid[]={
        {28,0xfffffff0},{44,0xffff},{100,0xffffffff},{116,0},{128,0x5001},
        {132,0},{132,4901},{136,4899},{136,0xffffffff},{140,7},{144,4096}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        memcpy(bad,mdt,sizeof(mdt)); put(bad+invalid[i].offset,invalid[i].value);
        struct lilac_a540_zap_layout before=result;
        assert(!lilac_a540_zap_layout(bad,sizeof(bad),4900,&result));
        assert(!memcmp(&before,&result,sizeof(result)));
    }
    puts("PASS: A540 split ELF metadata bounds, relocation, hash length and page extent");
}
