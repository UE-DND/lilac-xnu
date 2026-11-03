/* SPDX-License-Identifier: BSD-2-Clause
 * Sony ABL path: no UEFI mailbox, new DMA mapping, queue-base switch or writes.
 * Use the same bounded transport as the device-proven BootShim batch, through
 * Device mappings. Every request restores both inherited pages. Serialization
 * covers all controller accesses, never a user copy. Any failure retires IO.
 */
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include "ufs-read.h"
#include "lilac-ufs-api.h"
#include "lilac-image.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
static vm_offset_t controller, queue;
static uint32_t queue_pa;
static struct ur_result boot_result, io_result;
static struct lilac_ufs_info info;
static int bmajor=-1, cmajor=-1;
static bool ready, dead;
static bool boot_batch_running;
static bool ufs_quiet;
static uint32_t block_size[130];
static struct lilac_image_map *image_map;
static bool image_ready;
static uint64_t io_calls, io_blocks, io_errors;
#define LILAC_IMAGE_CACHE_SLOTS 256u
/* Only the validated immutable image is cached; physical partition reads and
 * registration's boundary checks always reach the controller. The IO mutex
 * protects both data and tags and the cache never hides a retired device. */
static struct { uint64_t lba; bool valid; uint8_t data[4096]; } image_cache[LILAC_IMAGE_CACHE_SLOTS];
static uint64_t io_cache_hits;
/* Four pages match a 16 KiB VM page; the transport still reads one 4 KiB
 * sector per command, but saves/restores the borrowed queue once per batch. */
#define LILAC_IO_BATCH 4u
static uint32_t bounce[(LILAC_IO_BATCH*4096)/4];
static LCK_GRP_DECLARE(lilac_ufs_group,"lilac-ufs");
static LCK_MTX_DECLARE(lilac_ufs_lock,&lilac_ufs_group);

uint32_t ur_reg(uint32_t offset);
void ur_set_reg(uint32_t offset,uint32_t value);
uint32_t ur_load(uint64_t address);
void ur_store(uint64_t address,uint32_t value);
uint64_t ur_ticks(void);
uint64_t ur_frequency(void);
void ur_barrier(void);
void lilac_ufs_init(void);

void ur_barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
uint32_t ur_reg(uint32_t offset) {
    if (!controller || offset>0x80 || (offset&3) || (offset>=0x38 && offset<=0x48))
        panic("lilac UFS register read outside whitelist");
    return *(volatile uint32_t *)(controller+offset);
}
void ur_set_reg(uint32_t offset,uint32_t value) {
    if (!((offset==0x60 && value<=1) || (offset==0x58 && value==1)))
        panic("lilac UFS MMIO write outside whitelist");
    *(volatile uint32_t *)(controller+offset)=value; ur_barrier();
}
static vm_offset_t queue_address(uint64_t pa) {
    if (!queue || (pa&3) || pa<queue_pa || pa>=queue_pa+8192ULL)
        panic("lilac UFS queue address outside borrowed pages");
    return queue+(vm_offset_t)(pa-queue_pa);
}
uint32_t ur_load(uint64_t pa) { return *(volatile uint32_t *)queue_address(pa); }
void ur_store(uint64_t pa,uint32_t value) { *(volatile uint32_t *)queue_address(pa)=value; }
uint64_t ur_ticks(void) { uint64_t v;__asm__ volatile("mrs %0,cntvct_el0":"=r"(v));return v; }
uint64_t ur_frequency(void) { uint64_t v;__asm__ volatile("mrs %0,cntfrq_el0":"=r"(v));return v; }
void lilac_ufs_read_progress(uint32_t stage,uint32_t detail) {
    /* Every command retains stage/LBA in its result; avoid flooding the screen. */
    (void)detail;
    if (!boot_batch_running || ufs_quiet) return;
    static uint32_t last;
    if (stage!=last && (stage==1 || stage==3 || stage==10 || stage==13 || stage==9)) {
        printf("lilac UFS: stage %u\n",stage);last=stage;
    }
}

static int valid_unit(uint32_t unit) {
    return unit<=info.parts || (unit==LILAC_IMAGE_MINOR && image_ready);
}
static uint64_t device_bytes(uint32_t minor_number) {
    if (minor_number==LILAC_IMAGE_MINOR) return image_map->image_bytes;
    if (!minor_number) return ((uint64_t)info.last_lba+1)*4096;
    const struct lilac_ufs_part_info *p=&info.part[minor_number-1];
    return (p->last-p->first+1)*4096;
}
static int read_bytes(uint32_t unit,uint64_t off,uint8_t *dst,uint64_t length) {
    if (!valid_unit(unit) || off>device_bytes(unit) || length>device_bytes(unit)-off) return EINVAL;
    lck_mtx_lock(&lilac_ufs_lock);
    int error=0;
    if (!ready || dead) { error=ENXIO;goto out; }
    uint64_t start=unit && unit!=LILAC_IMAGE_MINOR?info.part[unit-1].first:0;
    io_calls++;
    while (length) {
        uint32_t in=(uint32_t)(off&4095);
        uint32_t n=(uint32_t)(length>LILAC_IO_BATCH*4096-in?LILAC_IO_BATCH*4096-in:length);
        uint32_t count=(in+n+4095)/4096;
        uint64_t lba=start+off/4096;
        if (unit==LILAC_IMAGE_MINOR && !lilac_image_translate(image_map,off/4096,&lba)) {
            dead=true;io_errors++;error=EIO;break;
        }
        if (unit==LILAC_IMAGE_MINOR) {
            unsigned slot=(unsigned)(lba%LILAC_IMAGE_CACHE_SLOTS);
            if (image_cache[slot].valid && image_cache[slot].lba==lba) {
                uint32_t take=(uint32_t)(length>4096-in?4096-in:length);
                memcpy(dst,image_cache[slot].data+in,take);
                io_cache_hits++;off+=take;dst+=take;length-=take;continue;
            }
        }
        if (unit==LILAC_IMAGE_MINOR) {
            /* A file extent boundary may jump to another physical sector. */
            for (uint32_t i=1;i<count;i++) {
                uint64_t next;
                if (!lilac_image_translate(image_map,off/4096+i,&next)) {
                    dead=true;io_errors++;error=EIO;goto out;
                }
                if (next!=lba+i) {count=i;n=count*4096-in;break;}
            }
        }
        lilac_ufs_read_range(&io_result,info.last_lba,(uint32_t)lba,count,(uint8_t *)bounce);
        if (io_result.error || !io_result.restored || io_result.completed!=count) {
            dead=true;io_errors++;error=EIO;
            printf("lilac UFS IO STOP error=%u stage=%u lba=%llx restored=%u OCS=%x DB=%x\n",
                io_result.error,io_result.stage,lba,io_result.restored,io_result.ocs,io_result.doorbell);
            break;
        }
        if (unit==LILAC_IMAGE_MINOR) for (uint32_t i=0;i<count;i++) {
            unsigned slot=(unsigned)((lba+i)%LILAC_IMAGE_CACHE_SLOTS);
            memcpy(image_cache[slot].data,(uint8_t *)bounce+(size_t)i*4096,4096);
            image_cache[slot].lba=lba+i;image_cache[slot].valid=true;
        }
        memcpy(dst,(uint8_t *)bounce+in,n);io_blocks+=count;off+=n;dst+=n;length-=n;
    }
out:
    lck_mtx_unlock(&lilac_ufs_lock);return error;
}
static int disk_open(dev_t dev,int flags,__unused int type,__unused proc_t p) {
    if (!ready || dead || !valid_unit((uint32_t)minor(dev))) return ENXIO;
    return flags&FWRITE?EROFS:0;
}
static int disk_close(__unused dev_t dev,__unused int flags,__unused int type,__unused proc_t p) {return 0;}
static int disk_write(__unused dev_t dev,__unused struct uio *uio,__unused int flags) {return EROFS;}
static int disk_ioctl(dev_t dev,u_long cmd,caddr_t data,__unused int flags,__unused proc_t p) {
    uint32_t unit=(uint32_t)minor(dev);
    if (!ready || dead || !valid_unit(unit)) return ENXIO;
    int error=0;lck_mtx_lock(&lilac_ufs_lock);
    switch (cmd) {
    case DKIOCGETBLOCKSIZE: *(uint32_t *)data=block_size[unit];break;
    case DKIOCSETBLOCKSIZE:
        if (*(uint32_t *)data<512 || *(uint32_t *)data>4096 || (*(uint32_t *)data&(*(uint32_t *)data-1))) error=EINVAL;
        else block_size[unit]=*(uint32_t *)data;
        break;
    case DKIOCGETPHYSICALBLOCKSIZE: *(uint32_t *)data=4096;break;
    case DKIOCGETBLOCKCOUNT: *(uint64_t *)data=device_bytes(unit)/block_size[unit];break;
    case DKIOCISWRITABLE: *(uint32_t *)data=0;break;
    case DKIOCGETMAXBLOCKCOUNTREAD: *(uint64_t *)data=65536/block_size[unit];break;
    default:error=ENOTTY;break;
    }
    lck_mtx_unlock(&lilac_ufs_lock);return error;
}
static int disk_psize(dev_t dev) {return valid_unit((uint32_t)minor(dev))?(int)block_size[minor(dev)]:-1;}
static void disk_strategy(buf_t bp) {
    uint32_t unit=(uint32_t)minor(buf_device(bp));int error=0;
    uint64_t count=buf_count(bp), n=0;
    if (!ready || dead || !valid_unit(unit)) error=ENXIO;
    else if (!(buf_flags(bp)&B_READ)) error=EROFS;
    else if (buf_blkno(bp)<0) error=EINVAL;
    else {
        uint64_t size=device_bytes(unit), number=(uint64_t)buf_blkno(bp);
        uint32_t scale=block_size[unit];uint64_t off=number*scale;
        if (number>size/scale || off>size) error=EINVAL;
        else {n=count>size-off?size-off:count;error=read_bytes(unit,off,(uint8_t *)buf_dataptr(bp),n);}
    }
    buf_setresid(bp,(uint32_t)(error?count:count-n));if (error) buf_seterror(bp,error);buf_biodone(bp);
}
static int disk_read(dev_t dev,struct uio *uio,__unused int flags) {
    uint32_t unit=(uint32_t)minor(dev);int error=0;
    if (!ready || dead || !valid_unit(unit)) return ENXIO;
    if (uio_offset(uio)<0) return EINVAL;
    uint8_t *copy=kalloc_data(4096,Z_WAITOK);if (!copy) return ENOMEM;
    uint64_t size=device_bytes(unit);
    while (uio_resid(uio)>0 && (uint64_t)uio_offset(uio)<size) {
        uint64_t off=(uint64_t)uio_offset(uio), n=(uint64_t)uio_resid(uio);
        if (n>4096) n=4096;if (n>size-off) n=size-off;
        error=read_bytes(unit,off,copy,n);if (error) break;
        error=uiomove((caddr_t)copy,(int)n,uio);if (error) break;
    }
    kfree_data(copy,4096);return error;
}
static const struct bdevsw bops={.d_open=disk_open,.d_close=disk_close,.d_strategy=disk_strategy,
    .d_ioctl=disk_ioctl,.d_dump=eno_dump,.d_psize=disk_psize,.d_type=D_DISK};
static const struct cdevsw cops={.d_open=disk_open,.d_close=disk_close,.d_read=disk_read,.d_write=disk_write,
    .d_ioctl=disk_ioctl,.d_stop=eno_stop,.d_reset=eno_reset,.d_select=eno_select,.d_mmap=eno_mmap,
    .d_strategy=eno_strat,.d_reserved_1=eno_getc,.d_reserved_2=eno_putc,.d_type=D_DISK};
SYSCTL_OPAQUE(_kern,OID_AUTO,lilac_ufs_info,CTLFLAG_RD|CTLFLAG_LOCKED,&info,sizeof(info),"S,lilac_ufs_info","Read-only Lilac GPT catalog");
static int status_sysctl(__unused struct sysctl_oid *oidp,__unused void *arg1,__unused int arg2,struct sysctl_req *req) {
    char text[256];lck_mtx_lock(&lilac_ufs_lock);
    snprintf(text,sizeof(text),"ready=%u dead=%u parts=%u checks=%x calls=%llu blocks=%llu errors=%llu boot_commands=%u restored=%u cache_hits=%llu",
        ready,dead,info.parts,info.checks,io_calls,io_blocks,io_errors,info.commands,info.restored,io_cache_hits);
    lck_mtx_unlock(&lilac_ufs_lock);return SYSCTL_OUT(req,text,strlen(text)+1);
}
SYSCTL_PROC(_kern,OID_AUTO,lilac_ufs_status,CTLTYPE_STRING|CTLFLAG_RD|CTLFLAG_LOCKED,0,0,status_sysctl,"A","Mapped Sony UFS IO state");

static uint32_t image_crc(const uint8_t *p) {
    uint32_t crc=~0u;
    for (uint32_t i=0;i<4096;i++) {
        crc^=p[i];for (uint32_t bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    return ~crc;
}
static int image_sysctl(__unused struct sysctl_oid *oidp,__unused void *arg1,__unused int arg2,struct sysctl_req *req) {
    if (!req->newptr) return ENOTSUP;
    size_t bytes=req->newlen;
    if (bytes<sizeof(struct lilac_image_map) || bytes>sizeof(struct lilac_image_map)+LILAC_IMAGE_MAX_EXTENTS*sizeof(struct lilac_image_extent)) return EINVAL;
    struct lilac_image_map *m=kalloc_data(bytes,Z_WAITOK);if (!m) return ENOMEM;
    int error=SYSCTL_IN(req,m,bytes);if (error) goto fail;
    uint32_t ud=info.parts;
    for (uint32_t i=0;i<info.parts;i++) if (!strcmp(info.part[i].name,"userdata")) {ud=i;break;}
    if (!ready || dead || ud==info.parts || !lilac_image_validate(m,bytes,info.part[ud].first,info.part[ud].last,info.entries_crc)) {error=EINVAL;goto fail;}
    uint64_t first,last;uint8_t *copy=kalloc_data(4096,Z_WAITOK);
    if (!copy) {error=ENOMEM;goto fail;}
    if (!lilac_image_translate(m,0,&first) || !lilac_image_translate(m,m->image_bytes/4096-1,&last)) error=EINVAL;
    if (!error) error=read_bytes(0,first*4096,copy,4096);
    if (!error && (image_crc(copy)!=m->first_crc || memcmp(copy+32,"NXSB",4))) error=EINVAL;
    if (!error) error=read_bytes(0,last*4096,copy,4096);
    if (!error && image_crc(copy)!=m->last_crc) error=EINVAL;
    kfree_data(copy,4096);if (error) goto fail;
    lck_mtx_lock(&lilac_ufs_lock);
    if (image_map || !ready || dead) {lck_mtx_unlock(&lilac_ufs_lock);error=EBUSY;goto fail;}
    image_map=m;block_size[LILAC_IMAGE_MINOR]=4096;
    if (!devfs_make_node(makedev(bmajor,LILAC_IMAGE_MINOR),DEVFS_BLOCK,UID_ROOT,GID_OPERATOR,0440,"lilac_image") ||
        !devfs_make_node(makedev(cmajor,LILAC_IMAGE_MINOR),DEVFS_CHAR,UID_ROOT,GID_OPERATOR,0440,"rlilac_image")) {
        image_map=NULL;lck_mtx_unlock(&lilac_ufs_lock);error=ENXIO;goto fail;
    }
    image_ready=true;lck_mtx_unlock(&lilac_ufs_lock);
    if (!ufs_quiet) printf("lilac IMAGE: %llu bytes in %u immutable plaintext file extents; GPT+head/tail validated; read only\n",m->image_bytes,m->count);
    return 0;
fail:
    kfree_data(m,bytes);return error;
}
SYSCTL_PROC(_kern,OID_AUTO,lilac_ufs_image,CTLTYPE_OPAQUE|CTLFLAG_WR|CTLFLAG_LOCKED,0,0,image_sysctl,"S,lilac_image_map","Register immutable read-only file extents; no storage writes");

void lilac_ufs_init(void) {
    uint32_t enabled=0;
    if (!PE_parse_boot_argn("lilac_ufs",&enabled,sizeof(enabled)) || enabled!=1) return;
    uint32_t quiet=0;PE_parse_boot_argn("lilac_quiet",&quiet,sizeof(quiet));ufs_quiet=quiet==1;
    if (!ufs_quiet) printf("lilac UFS: mapping controller + borrowed pages as Device memory\n");
    controller=ml_io_map(0x01da4000,0x4000);if (!controller) return;
    queue_pa=ur_reg(0x50);uint32_t tm=ur_reg(0x70);
    if (queue_pa<0x99000000 || queue_pa>0x9b000000-8192 || (queue_pa&4095) || tm!=queue_pa+4096 || ur_reg(0x54) || ur_reg(0x74)) {
        printf("lilac UFS: invalid queue mapping; disabled\n");return;
    }
    queue=ml_io_map(queue_pa,8192);if (!queue) return;
    uint32_t fast=0;PE_parse_boot_argn("lilac_ufs_fast",&fast,sizeof(fast));
    boot_batch_running=true;
    if (fast==1) lilac_ufs_read_probe_fast(&boot_result);else lilac_ufs_read_probe(&boot_result);
    boot_batch_running=false;
    if (!ufs_quiet || boot_result.error) printf("lilac UFS KERNEL BATCH: error=%u checks=%x commands=%u parts=%u sampled=%u restored=%u ms=%u\n",
        boot_result.error,boot_result.checks,boot_result.completed,boot_result.partitions,boot_result.sampled,boot_result.restored,boot_result.elapsed_ms);
    if (boot_result.error || boot_result.checks!=(fast==1?47u:63u) || !boot_result.restored) {
        dead=true;printf("lilac UFS: stage=%u lba=%x OCS=%x IS=%x DB=%x; device disabled\n",
            boot_result.stage,boot_result.failed_lba,boot_result.ocs,boot_result.is_after,boot_result.doorbell);return;
    }
    info.version=LILAC_UFS_API_VERSION;info.parts=boot_result.partitions;info.block_bytes=4096;
    info.last_lba=boot_result.last_lba;info.checks=boot_result.checks;info.commands=boot_result.completed;info.restored=boot_result.restored;
    info.mbr_crc=boot_result.mbr_crc;info.header_crc=boot_result.primary_crc;info.entries_crc=boot_result.entries_crc;
    for (uint32_t i=0;i<info.parts;i++) {
        const struct ur_partition *p=&boot_result.partition[i];struct lilac_ufs_part_info *q=&info.part[i];
        memcpy(q->name,p->name,37);q->first=p->first;q->last=p->last;q->head_crc=p->head_crc;q->tail_crc=p->tail_crc;
        if (!ufs_quiet) printf("lilac GPT %u %s LBA=%llx..%llx head=%08x tail=%08x\n",i+1,q->name,q->first,q->last,q->head_crc,q->tail_crc);
    }
    bmajor=bdevsw_add(-1,&bops);cmajor=bmajor<0?-1:cdevsw_add_with_bdev(-1,&cops,bmajor);
    if (cmajor<0) {printf("lilac UFS: device allocation failed\n");return;}
    for (uint32_t i=0;i<=info.parts;i++) {
        block_size[i]=4096;
        if (!devfs_make_node(makedev(bmajor,i),DEVFS_BLOCK,UID_ROOT,GID_OPERATOR,0440,"lilac%u",i) ||
            !devfs_make_node(makedev(cmajor,i),DEVFS_CHAR,UID_ROOT,GID_OPERATOR,0440,"rlilac%u",i)) {
            dead=true;printf("lilac UFS: node creation failed; all devices disabled\n");return;
        }
    }
    ready=true;info.ready=1;
    if (!ufs_quiet) printf("lilac UFS: /dev/lilac0 + /dev/rlilac0 whole LUN; 1..%u GPT partitions; STRICT READ ONLY\n",info.parts);
}
