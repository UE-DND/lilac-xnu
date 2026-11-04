#!/usr/bin/env python3
"""Execute actual XNU adapter device methods with fake buf/uio and transport."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'kernel/lilac-ufs.c').read_text()

def function(name):
    # These methods contain no literal braces in comments or strings.
    import re
    m = re.search(r'^static (?:uint64_t|int|void) '+name+r'\(', source, re.M)
    assert m, name
    start = source.index('{', m.start())
    depth = 1
    end = start+1
    while depth:
        depth += (source[end]=='{') - (source[end]=='}')
        end += 1
    return source[m.start():end]

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#define __unused __attribute__((unused))
#pragma clang diagnostic ignored "-Wformat"
#endif
#include "ufs-read.h"
#include "lilac-ufs-api.h"
#include "lilac-image.h"
#define FWRITE 2
#define B_READ 1
#define Z_WAITOK 0
typedef void *proc_t;
enum {DKIOCGETBLOCKSIZE=1,DKIOCSETBLOCKSIZE,DKIOCGETPHYSICALBLOCKSIZE,
      DKIOCGETBLOCKCOUNT,DKIOCISWRITABLE,DKIOCGETMAXBLOCKCOUNTREAD};
static struct lilac_ufs_info info;
static struct ur_result io_result;
static bool ready,dead;
#define LILAC_IO_BATCH 4u
static uint32_t block_size[130],bounce[LILAC_IO_BATCH*1024];
static struct lilac_image_map *image_map;
static bool image_ready;
static uint64_t io_calls,io_blocks,io_errors,io_batches,io_ticks;
#define LILAC_IMAGE_CACHE_SLOTS 256u
static struct {uint64_t lba;bool valid;uint8_t data[4096];} image_cache[LILAC_IMAGE_CACHE_SLOTS];
static uint64_t io_cache_hits;
static int lilac_ufs_lock,held;
static unsigned reads,fail_at;
static uint64_t ur_ticks(void) {static uint64_t ticks;return ++ticks;}
static void lck_mtx_lock(int *p) {(void)p;assert(!held);held=1;}
static void lck_mtx_unlock(int *p) {(void)p;assert(held);held=0;}
static void *kalloc_data(size_t n,int f) {(void)f;return malloc(n);}
static void kfree_data(void *p,size_t n) {(void)n;free(p);}
static uint8_t pattern(uint64_t offset) {return (uint8_t)((offset*73+(offset>>8)*19+41)&255);}
void lilac_ufs_read_range(struct ur_result *r,uint32_t last,uint32_t lba,uint32_t count,uint8_t *dst) {
    assert(held && last==99 && lba<=99 && count>=1 && count<=LILAC_IO_BATCH && count-1<=last-lba);reads++;
    memset(r,0,sizeof(*r));
    if (fail_at==reads) {r->error=UR_IO_TIMEOUT;return;}
    for (unsigned i=0;i<count*4096;i++) dst[i]=pattern((uint64_t)lba*4096+i);
    r->restored=1;r->completed=count;
}
struct uio {int64_t off,resid;uint8_t *dst;};
static int64_t uio_offset(struct uio *p) {return p->off;}
static int64_t uio_resid(struct uio *p) {return p->resid;}
static int uiomove(caddr_t from,int n,struct uio *p) {
    assert(!held && n<=p->resid);memcpy(p->dst,from,(size_t)n);p->off+=n;p->resid-=n;p->dst+=n;return 0;
}
struct buffer {dev_t unit;int64_t block;uint32_t flags,count,resid;int error,done;uint8_t *dst;};
typedef struct buffer *buf_t;
static dev_t buf_device(buf_t b) {return b->unit;}
static int64_t buf_blkno(buf_t b) {return b->block;}
static uint32_t buf_flags(buf_t b) {return b->flags;}
static uint32_t buf_count(buf_t b) {return b->count;}
static uintptr_t buf_dataptr(buf_t b) {return (uintptr_t)b->dst;}
static void buf_setresid(buf_t b,uint32_t n) {b->resid=n;}
static void buf_seterror(buf_t b,int e) {b->error=e;}
static void buf_biodone(buf_t b) {assert(!held);b->done++;}
'''
body = '\n'.join(function(name) for name in ('valid_unit','device_bytes','read_bytes','disk_open','disk_close',
    'disk_write','disk_ioctl','disk_psize','disk_strategy','disk_read'))
tests = r'''
static void setup(void) {
    memset(&info,0,sizeof(info));info.parts=1;info.last_lba=99;
    info.part[0].first=10;info.part[0].last=19;
    ready=true;dead=false;image_ready=false;image_map=NULL;held=0;reads=fail_at=0;io_calls=io_blocks=io_errors=io_cache_hits=io_batches=io_ticks=0;
    memset(image_cache,0,sizeof(image_cache));
    block_size[0]=block_size[1]=4096;
}
int main(void) {
    uint8_t b[16385],copy[16385];uint64_t count;uint32_t size;
    setup();assert(!disk_open(0,0,0,NULL) && !disk_open(1,0,0,NULL));
    assert(disk_open(0,FWRITE,0,NULL)==EROFS && disk_open(2,0,0,NULL)==ENXIO);
    assert(!disk_close(0,0,0,NULL) && disk_write(0,NULL,0)==EROFS && reads==0);
    size=0;assert(!disk_ioctl(1,DKIOCISWRITABLE,(caddr_t)&size,0,NULL) && size==0);
    assert(!disk_ioctl(1,DKIOCGETBLOCKCOUNT,(caddr_t)&count,0,NULL) && count==10);
    size=512;assert(!disk_ioctl(1,DKIOCSETBLOCKSIZE,(caddr_t)&size,0,NULL));
    assert(!disk_ioctl(1,DKIOCGETBLOCKCOUNT,(caddr_t)&count,0,NULL) && count==80);
    size=513;assert(disk_ioctl(1,DKIOCSETBLOCKSIZE,(caddr_t)&size,0,NULL)==EINVAL);
    assert(disk_psize(1)==512 && disk_psize(2)==-1 && disk_ioctl(1,99,(caddr_t)&size,0,NULL)==ENOTTY);
    memset(b,0xa5,sizeof(b));assert(!read_bytes(1,4096-17,b,sizeof(b)));
    for (unsigned i=0;i<sizeof(b);i++) assert(b[i]==pattern(10*4096+4096-17+i));
    assert(reads==2 && io_batches==2 && io_ticks==2 && !held);
    unsigned before=reads;
    assert(read_bytes(2,0,b,1)==EINVAL && read_bytes(1,40960,b,1)==EINVAL &&
        read_bytes(1,UINT64_MAX,b,1)==EINVAL && read_bytes(1,0,b,UINT64_MAX)==EINVAL && reads==before);
    struct uio u={.off=40960-7,.resid=31,.dst=b};memset(b,0xa5,sizeof(b));
    assert(!disk_read(1,&u,0) && u.resid==24 && u.off==40960 && b[7]==0xa5);
    before=reads;u=(struct uio){.off=40960,.resid=1,.dst=b};assert(!disk_read(1,&u,0) && u.resid==1 && reads==before);
    u.off=40961;assert(!disk_read(1,&u,0) && reads==before);
    u.off=-1;assert(disk_read(1,&u,0)==EINVAL && reads==before);
    struct buffer bp={.unit=1,.block=1,.flags=B_READ,.count=4096,.dst=copy};
    disk_strategy(&bp);assert(bp.done==1 && bp.error==0 && bp.resid==0);
    for (unsigned i=0;i<4096;i++) assert(copy[i]==pattern(10*4096+512+i));
    before=reads;bp=(struct buffer){.unit=1,.block=0,.flags=0,.count=4096,.dst=copy};
    disk_strategy(&bp);assert(bp.error==EROFS && bp.resid==4096 && bp.done==1 && reads==before);
    bp=(struct buffer){.unit=1,.block=INT64_MAX,.flags=B_READ,.count=4096,.dst=copy};
    disk_strategy(&bp);assert(bp.error==EINVAL && bp.done==1 && reads==before);
    bp=(struct buffer){.unit=1,.block=80,.flags=B_READ,.count=4096,.dst=copy};
    disk_strategy(&bp);assert(!bp.error && bp.resid==4096 && bp.done==1 && reads==before);
    setup();fail_at=2;assert(read_bytes(1,0,b,16385)==EIO && dead && io_errors==1 && reads==2);
    assert(read_bytes(1,0,b,1)==ENXIO && reads==2 && disk_open(1,0,0,NULL)==ENXIO);
    u=(struct uio){.off=0,.resid=1,.dst=b};assert(disk_read(1,&u,0)==ENXIO && reads==2);
    setup();uint64_t storage[(sizeof(struct lilac_image_map)+2*sizeof(struct lilac_image_extent))/8];
    memset(storage,0,sizeof(storage));image_map=(struct lilac_image_map *)storage;
    image_map->image_bytes=3*4096;image_map->count=2;
    image_map->extent[0]=(struct lilac_image_extent){0,10,1};image_map->extent[1]=(struct lilac_image_extent){1,18,2};
    image_ready=true;block_size[LILAC_IMAGE_MINOR]=4096;
    assert(!read_bytes(LILAC_IMAGE_MINOR,4096-17,b,8192));assert(reads==2);
    for (unsigned i=0;i<8192;i++) {
        uint64_t at=4096-17+i,physical=(at/4096==0?10:17+at/4096)*4096+(at&4095);
        assert(b[i]==pattern(physical));
    }
    before=reads;memset(copy,0,sizeof(copy));
    assert(!read_bytes(LILAC_IMAGE_MINOR,4096-17,copy,8192));
    assert(reads==before && io_cache_hits==3 && !memcmp(b,copy,8192));
    /* Whole-device reads never use the image cache. */
    assert(!read_bytes(0,10*4096,copy,100) && reads==before+1);
    dead=true;assert(read_bytes(LILAC_IMAGE_MINOR,0,copy,1)==ENXIO && reads==before+1);
    dead=false;
    image_cache[10].valid=true;image_cache[10].lba=10+LILAC_IMAGE_CACHE_SLOTS;
    before=reads;assert(!read_bytes(LILAC_IMAGE_MINOR,0,copy,1) && reads==before+1);
    assert(copy[0]==pattern(10*4096) && image_cache[10].lba==10);
    image_cache[10].valid=false;before=reads;fail_at=reads+1;
    assert(read_bytes(LILAC_IMAGE_MINOR,0,copy,1)==EIO && dead && !image_cache[10].valid && reads==before+1);
    dead=false;fail_at=0;
    assert(disk_open(LILAC_IMAGE_MINOR,FWRITE,0,NULL)==EROFS && device_bytes(LILAC_IMAGE_MINOR)==12288);
    assert(read_bytes(LILAC_IMAGE_MINOR,12288,b,1)==EINVAL);
    puts("PASS: actual XNU device methods; RO open/write/strategy; unaligned char/block copy; EOF/residual; ioctl geometry; overflow; lock/user-copy separation; fail-stop");
}
'''
with tempfile.TemporaryDirectory(prefix='ufs-kernel-') as tmp:
    p=Path(tmp)/'test.c';binary=p.with_suffix('')
    p.write_text(prefix+'\n'+body+'\n'+tests)
    subprocess.run(['clang','-std=c11','-D_DARWIN_C_SOURCE','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I',str(ROOT/'kernel'),'-I',str(ROOT/'bootloader'),
                    str(p),str(ROOT/'kernel/lilac-image.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
