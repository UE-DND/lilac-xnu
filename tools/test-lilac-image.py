#!/usr/bin/env python3
"""Exercise the actual immutable image mapping validator and translator."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lilac-image.h"
static uint64_t storage[(sizeof(struct lilac_image_map)+4096*sizeof(struct lilac_image_extent))/8];
static void setup(void) {
    memset(storage,0,sizeof(storage));struct lilac_image_map *m=(void *)storage;
    m->magic=LILAC_IMAGE_MAGIC;m->version=1;m->image_bytes=10*4096;
    m->userdata_first=100;m->userdata_last=999;m->gpt_crc=0x1234;m->count=3;
    m->extent[0]=(struct lilac_image_extent){0,200,3};
    m->extent[1]=(struct lilac_image_extent){3,100,2};
    m->extent[2]=(struct lilac_image_extent){5,900,5};
}
int main(void) {
    setup();struct lilac_image_map *m=(void *)storage;size_t bytes=sizeof(*m)+3*sizeof(m->extent[0]);
    assert(lilac_image_validate(m,bytes,100,999,0x1234));
    for (uint64_t i=0;i<10;i++) {uint64_t p;assert(lilac_image_translate(m,i,&p));assert(p==(i<3?200+i:i<5?100+i-3:900+i-5));}
    uint64_t p;assert(!lilac_image_translate(m,10,&p) && !lilac_image_translate(m,UINT64_MAX,&p));
    assert(!lilac_image_translate(NULL,0,&p) && !lilac_image_translate(m,0,NULL));
    assert(!lilac_image_validate(m,bytes-1,100,999,0x1234));
    assert(!lilac_image_validate(m,bytes,99,999,0x1234));
    assert(!lilac_image_validate(m,bytes,100,998,0x1234));
    assert(!lilac_image_validate(m,bytes,100,999,0x1235));
    for (unsigned which=0;which<16;which++) {
        setup();
        switch(which) {
        case 0:m->magic^=1;break;case 1:m->version++;break;case 2:m->count=0;break;
        case 3:m->count=4097;break;case 4:m->image_bytes=0;break;case 5:m->image_bytes++;break;
        case 6:m->extent[0].logical=1;break;case 7:m->extent[1].logical++;break;
        case 8:m->extent[0].blocks=0;break;case 9:m->extent[2].blocks=UINT64_MAX;break;
        case 10:m->extent[0].physical=99;break;case 11:m->extent[0].physical=999;break;
        case 12:m->extent[1].physical=201;break;case 13:m->extent[0].physical=UINT64_MAX;break;
        case 14:m->reserved=1;break;case 15:m->extent[2].blocks--;break;
        }
        assert(!lilac_image_validate(m,bytes,100,999,0x1234));
    }
    setup();m->count=4096;m->image_bytes=4096ULL*4096;m->userdata_last=10000;
    for (unsigned i=0;i<4096;i++)m->extent[i]=(struct lilac_image_extent){i,200+2*i,1};
    assert(lilac_image_validate(m,sizeof(storage),100,10000,0x1234));
    for(unsigned i=0;i<4096;i++){assert(lilac_image_translate(m,i,&p));assert(p==200+2*i);}
    puts("PASS: production image validator/translator; holes, overlaps, integer overflow, out-of-partition, stale GPT, exact length and 4096 extents");
}
'''
with tempfile.TemporaryDirectory(prefix='image-map-') as tmp:
    p=Path(tmp)/'test.c';binary=p.with_suffix('');p.write_text(program)
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I',str(ROOT/'kernel'),str(p),str(ROOT/'kernel/lilac-image.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
