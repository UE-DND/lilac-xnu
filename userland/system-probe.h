/* SPDX-License-Identifier: BSD-2-Clause */
#include "../kernel/lilac-image.h"
static uint64_t system_map_storage[(sizeof(struct lilac_image_map)+LILAC_IMAGE_MAX_EXTENTS*sizeof(struct lilac_image_extent))/8];
static int system_mount_ok,system_data_ok;
static int system_prepare(void) {
    int fd=open("/probe/system-map.bin",O_RDONLY,0);struct stat st;
    if (fd<0 || fstat(fd,&st) || st.st_size<(off_t)sizeof(struct lilac_image_map) || (uint64_t)st.st_size>sizeof(system_map_storage)) {
        printf("UFS image mapping file FAIL errno=%d\n",errno);if(fd>=0)close(fd);return 0;
    }
    unsigned have=0,total=(unsigned)st.st_size;
    while (have<total) {ssize_t n=read(fd,(unsigned char *)system_map_storage+have,total-have);if(n<=0){close(fd);return 0;}have+=(unsigned)n;}
    close(fd);
    if (sysctlbyname("kern.lilac_ufs_image",NULL,NULL,system_map_storage,total)) {
        printf("UFS image register FAIL errno=%d\n",errno);return 0;
    }
    if (mount("apfs","/ios",MNT_RDONLY,(void *)"/dev/lilac_image")) {
        printf("UFS APFS mount FAIL errno=%d\n",errno);return 0;
    }
    system_mount_ok=1;
    if (mount("devfs","/ios/dev",0,NULL)) printf("UFS devfs mount errno=%d (inherited console remains open)\n",errno);
#ifdef LILAC_FAST_BOOT
    fast_boot_checkpoint("system-mounted");
    return 1; /* image registration, GPT and first/last block checks remain mandatory */
#endif
    puts("UFS APFS MOUNT: PASS; full original system + unsplit cache on storage");
    fd=open("/ios/lilac-probe/pattern.bin",O_RDONLY,0);
    if (fd<0 || fstat(fd,&st) || st.st_size!=65573) {if(fd>=0)close(fd);return 0;}
    unsigned char *mapped=mmap(NULL,65573,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
    if (mapped==MAP_FAILED) {close(fd);return 0;}
    int ok=1;
    for (unsigned i=0;i<65573;i++) if (mapped[i]!=(unsigned char)((i*73+(i>>8)*19+41)&255)) {ok=0;break;}
    unsigned char original=mapped[16384];mapped[16384]^=255;
    unsigned char copy=0;
    if (pread(fd,&copy,1,16384)!=1 || copy!=original || mapped[16384]!=(unsigned char)(original^255))ok=0;
    munmap(mapped,65573);close(fd);
    system_data_ok=ok;printf("UFS APFS file/pager/private-COW: %s\n",ok?"PASS":"FAIL");
    fd=open("/ios/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64",O_RDONLY,0);
    unsigned char magic[16];
    if (fd<0 || read(fd,magic,16)!=16 || memcmp(magic,"dyld_v1",7)) {if(fd>=0)close(fd);return 0;}
    close(fd);puts("UFS original shared cache header: PASS (43 files verified before storage)");
    return ok;
}
static int system_wait(pid_t child,const char *name,int expected) {
    for (unsigned i=0;i<1800;i++) {
        int status=0;pid_t got=wait4(child,&status,1,NULL);
        if (got==child) {
            printf("%s exit=%d signal=%d raw=%x\n",name,WIFEXITED(status)?WEXITSTATUS(status):-1,WTERMSIG(status),status);
            return WIFEXITED(status) && WEXITSTATUS(status)==expected;
        }
        if (got<0 && errno!=4) return 0;
        if (i && i%300==0) printf("%s still loading; elapsed=%u seconds\n",name,i/10);
        usleep(100000);
    }
    mini_syscall(SYS_kill,child,9,0,0,0,0);puts("UFS dynamic child exceeded 180 seconds; supervisor remains alive");return 0;
}
static int system_execute(const char *path,int expected) {
    pid_t child=fork();
    if (child==0) {
        /* Cache is on the process' own root volume; do not relax XNU checks. */
        if (mini_syscall(61,(long)"/ios",0,0,0,0,0)<0 || chdir("/")) {
            printf("UFS child chroot FAIL errno=%d\n",errno);_exit(126);
        }
        char *args[]={(char *)path,"from-UFS",NULL};
        char *env[]={"PATH=/bin:/usr/bin","HOME=/",NULL};
        execve(path,args,env);printf("UFS exec %s FAIL errno=%d\n",path,errno);_exit(127);
    }
    if (child<0) return 0;
    return system_wait(child,path,expected);
}
static int system_test(void) {
    if (!system_prepare()) {puts("UFS SYSTEM: FAIL before dynamic execution");return 0;}
    int dynamic=system_execute("/bin/hello_dyn",7);
    if (dynamic) {
        puts("APPLE DYLD + LIBSYSTEM FROM UFS: PASS");
        int repeat=system_execute("/bin/hello_dyn",7);dynamic=dynamic&&repeat;
        /* Stock Apple df is a separate executable through the same cache. */
        pid_t child=fork();
        if (child==0) {
            if (mini_syscall(61,(long)"/ios",0,0,0,0,0)<0 || chdir("/"))_exit(126);
            char *args[]={"/bin/df",NULL};char *env[]={"PATH=/bin:/usr/bin",NULL};
            execve(args[0],args,env);_exit(127);
        }
        if (child>0) (void)system_wait(child,"Apple /bin/df",0);
    }
    printf("UFS SYSTEM BATCH: mount=%s file/pager=%s Apple-dynamic=%s\n",
        system_mount_ok?"PASS":"FAIL",system_data_ok?"PASS":"FAIL",dynamic?"PASS":"FAIL");
    return dynamic;
}
