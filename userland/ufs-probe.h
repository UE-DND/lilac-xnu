/* SPDX-License-Identifier: BSD-2-Clause
 * Batch process-context checks. The device is intrinsically read-only; attempts
 * to open for writing exercise rejection and never acquire a writable handle.
 */
#include "../kernel/lilac-ufs-api.h"
static struct lilac_ufs_info ufs_info;
static unsigned char ufs_a[3*4096+37],ufs_b[3*4096+37];
static uint32_t ufs_crc(const unsigned char *p,unsigned n) {
    uint32_t crc=~0u;
    for (unsigned i=0;i<n;i++) {
        crc^=p[i];for (unsigned k=0;k<8;k++) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    return ~crc;
}
static void ufs_path(char *path,int raw,unsigned number) {
    strcpy(path,raw?"/dev/rlilac":"/dev/lilac");
    unsigned at=(unsigned)strlen(path);char digits[4];unsigned n=0;
    do {digits[n++]=(char)('0'+number%10);number/=10;} while (number);
    while (n) path[at++]=digits[--n];path[at]=0;
}
static int ufs_ro_open(const char *path) {
    int fd=open(path,O_RDWR,0);
    if (fd>=0) {close(fd);return 0;}
    return errno==30 || errno==13; /* EROFS, or devfs mode denies access first. */
}
static int ufs_test(void) {
    size_t length=sizeof(ufs_info);
    if (sysctlbyname("kern.lilac_ufs_info",&ufs_info,&length,NULL,0)!=0 || length!=sizeof(ufs_info) ||
        ufs_info.version!=LILAC_UFS_API_VERSION || !ufs_info.ready || !ufs_info.parts ||
        ufs_info.parts>LILAC_UFS_MAX_PARTS || ufs_info.block_bytes!=4096 || ufs_info.checks!=63 || !ufs_info.restored) {
        printf("UFS catalog/kernel batch FAIL errno=%d size=%lu\n",errno,(unsigned long)length);return 0;
    }
    printf("XNU UFS: %u partitions, %u boot reads, full GPT checks=%x restored=%u\n",
        ufs_info.parts,ufs_info.commands,ufs_info.checks,ufs_info.restored);
    unsigned checked=0;int ok=1;
    for (unsigned i=0;i<=ufs_info.parts;i++) {
        char raw[32],block[32];ufs_path(raw,1,i);ufs_path(block,0,i);
        int a=open(raw,O_RDONLY,0),b=open(block,O_RDONLY,0);
        if (a<0 || b<0) {printf("UFS open FAIL index=%u errno=%d\n",i,errno);ok=0;if (a>=0)close(a);if(b>=0)close(b);break;}
        uint64_t bytes=i?(ufs_info.part[i-1].last-ufs_info.part[i-1].first+1)*4096:((uint64_t)ufs_info.last_lba+1)*4096;
        if (pread(a,ufs_a,4096,0)!=4096 || pread(b,ufs_b,4096,0)!=4096 || memcmp(ufs_a,ufs_b,4096)) goto bad;
        uint32_t head=ufs_crc(ufs_a,4096);
        if (head!=(i?ufs_info.part[i-1].head_crc:ufs_info.mbr_crc)) goto bad;
        if (pread(a,ufs_b,4096,(off_t)(bytes-4096))!=4096) goto bad;
        if (i && ufs_crc(ufs_b,4096)!=ufs_info.part[i-1].tail_crc) goto bad;
        /* Cross a 4KiB DMA boundary, also a 16KiB VM boundary on large units. */
        if (bytes>=5*4096) {
            off_t at=16384-23;
            if (pread(a,ufs_a,101,at)!=101 || pread(b,ufs_b,101,at)!=101 || memcmp(ufs_a,ufs_b,101)) goto bad;
        }
        if (pread(a,ufs_a,31,(off_t)(bytes-7))!=7 || pread(a,ufs_a,1,(off_t)bytes)!=0 ||
            pread(a,ufs_a,1,(off_t)(bytes+1))!=0 || !ufs_ro_open(raw) || !ufs_ro_open(block)) goto bad;
        close(a);close(b);checked++;continue;
bad:
        printf("UFS data/bounds/RO FAIL index=%u name=%s errno=%d\n",i,i?ufs_info.part[i-1].name:"LUN0",errno);
        close(a);close(b);ok=0;break;
    }
    /* Repeated 12KiB+ reads validate copying and per-page snapshot/restore. */
    int fd=open("/dev/rlilac0",O_RDONLY,0);
    if (fd<0) ok=0;
    else {
        for (unsigned i=0;i<4 && ok;i++) {
            off_t off=(off_t)(i*16384+4096-17);
            if (pread(fd,ufs_a,sizeof(ufs_a),off)!=(ssize_t)sizeof(ufs_a) ||
                pread(fd,ufs_b,sizeof(ufs_b),off)!=(ssize_t)sizeof(ufs_b) || memcmp(ufs_a,ufs_b,sizeof(ufs_a))) ok=0;
        }
        close(fd);
    }
    char status[256];length=sizeof(status);
    if (sysctlbyname("kern.lilac_ufs_status",status,&length,NULL,0)!=0) ok=0;
    else {printf("UFS runtime: %s\n",status);if (strncmp(status,"ready=1 dead=0",14)) ok=0;}
    printf("UFS USERSPACE: %s; %u/%u char+block devices; head/tail CRC, unaligned, EOF, RO, repeat\n",
        ok?"PASS":"FAIL",checked,ufs_info.parts+1);
    return ok;
}
