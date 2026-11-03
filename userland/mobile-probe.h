/* SPDX-License-Identifier: BSD-2-Clause
 * One RAM boot: runtime batch, volatile /var, then real PID1 launchd and services.
 */
#include "var-skeleton.h"
static unsigned mobile_passed,mobile_total;
static unsigned mobile_results[8];
static const char *mobile_names[]={"pthread","mach-ipc","kqueue","gcd","unix-socket","xpc-local","ramfs","Foundation"};
static int mobile_var_ok;

static int mobile_dir(const char *path) {
    struct stat st;
    if(stat(path,&st)==0)return (st.st_mode&S_IFMT)==S_IFDIR;
    return sysctlbyname("kern.apfs_synthetic",NULL,NULL,path,strlen(path))==0;
}
static int mobile_var(void) {
    if(mount("ramfs","/ios/private/var",0,NULL)) {printf("MOBILE /var ramfs FAIL errno=%d\n",errno);return 0;}
    unsigned made=0;
    for(unsigned i=0;i<sizeof(var_dirs)/sizeof(var_dirs[0]);i++) {
        char path[512];strcpy(path,"/ios");strcpy(path+4,var_dirs[i].path);
        if((mkdir(path,var_dirs[i].mode) && errno!=17) || chmod(path,var_dirs[i].mode) ||
            chown(path,var_dirs[i].uid,var_dirs[i].gid)) {
            printf("MOBILE skeleton FAIL %s errno=%d\n",path,errno);return 0;
        }
        made++;
    }
    for(unsigned i=0;i<sizeof(var_links)/sizeof(var_links[0]);i++) {
        char path[512];strcpy(path,"/ios");strcpy(path+4,var_links[i].path);
        if(symlink(var_links[i].target,path) && errno!=17)return 0;
    }
    const char *extra[]={"/ios/private/var/log/com.apple.xpc.launchd","/ios/private/var/db/com.apple.xpc.launchd","/ios/private/var/db/launchd.db"};
    for(unsigned i=0;i<sizeof(extra)/sizeof(extra[0]);i++)if(mkdir(extra[i],0755)&&errno!=17)return 0;
    printf("MOBILE writable /var: PASS; %u original template directories, RAM only\n",made);return 1;
}
/* Reap even a killed child; never leave a loading experiment unbounded. */
static int mobile_wait(pid_t pid,const char *label,unsigned seconds) {
    int killed=0;
    for(unsigned i=0;i<(seconds+5)*10;i++) {
        int status=0;pid_t r=wait4(pid,&status,1,NULL);
        if(r==pid) {
            int ok=!killed && WIFEXITED(status) && WEXITSTATUS(status)==0;
            printf("MOBILE %s: %s exit=%d signal=%d\n",label,ok?"PASS":"FAIL",WIFEXITED(status)?WEXITSTATUS(status):-1,WTERMSIG(status));return ok;
        }
        if(r<0 && errno!=4){printf("MOBILE wait %s errno=%d\n",label,errno);return 0;}
        if(i==seconds*10){killed=1;mini_syscall(SYS_kill,pid,9,0,0,0,0);printf("MOBILE %s timeout %u seconds\n",label,seconds);}
        usleep(100000);
    }
    return 0;
}
static int mobile_run(const char *path,const char *arg,int chrooted,unsigned seconds) {
    pid_t child=fork();
    if(child==0) {
        if(!chrooted && (mini_syscall(61,(long)"/ios",0,0,0,0,0)<0 || chdir("/")))_exit(126);
        char *argv[]={(char *)path,(char *)arg,NULL};
        char *env[]={"PATH=/bin:/usr/bin:/usr/sbin:/sbin","HOME=/var/root","TMPDIR=/var/tmp","LANG=C",NULL};
        execve(path,argv,env);printf("MOBILE exec %s errno=%d\n",path,errno);_exit(127);
    }
    return child>0 && mobile_wait(child,arg?arg:path,seconds);
}
static void mobile_report(void) {
    printf("MOBILE RUNTIME: %u/%u; RAM-var=%s; UFS mount/paging/dynamic PASS\n",mobile_passed,mobile_total,mobile_var_ok?"PASS":"FAIL");
    for(unsigned i=0;i<mobile_total;i++)printf("  %s=%s\n",mobile_names[i],mobile_results[i]?"PASS":"FAIL");
}
/* Launched by stock launchd through xpcproxy; PID1's old supervisor is gone. */
static int mobile_job(void) {
    puts("APPLE LAUNCHD JOB STARTED: static job runner + native service client");
    printf("launchd job pid=%d ppid=%d\n",getpid(),getppid());
#if LILAC_DESKTOP_ATTEMPT
    pid_t watcher=fork();
    if(watcher==0) {
        char *argv[]={"/lilac-probe/bin/desktop_watch",NULL};
        char *env[]={"PATH=/bin:/usr/bin:/usr/sbin:/sbin","HOME=/var/root",NULL};
        execve(argv[0],argv,env);_exit(127);
    }
    if(watcher<0)puts("DESKTOP watcher fork failed");
#endif
    int fd=open("/private/var/run/lilac-launchd-job",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){const char *s="Apple launchd spawned com.lilac.port.services\n";write(fd,s,strlen(s));close(fd);}
    int ok=mobile_run("/lilac-probe/bin/services_probe",NULL,1,60);
    (void)mobile_run("/bin/ps","-A",1,15);
    int surfaces=mobile_run("/lilac-probe/bin/surface_probe",NULL,1,30);
    fd=open("/private/var/run/lilac-surface.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){dprintf(fd,"IOSURFACE sharing/planes: %s\n",surfaces?"PASS":"FAIL");close(fd);}
    int display=mobile_run("/lilac-probe/bin/display_probe",NULL,1,30);
    fd=open("/private/var/run/lilac-graphics.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){dprintf(fd,"APPLE GRAPHICS: FAIL\nframework process did not start\n");close(fd);}
    int graphics=mobile_run("/lilac-probe/bin/graphics_probe",NULL,1,60);
    const char *frameworks[]={"/lilac-probe/bin/layer_probe","/lilac-probe/bin/uikit_probe"};
    const char *statuses[]={"/private/var/run/lilac-ca.status","/private/var/run/lilac-uikit.status"};
    int framework_results[2]={0};
    for(unsigned i=0;i<2;i++) {
        fd=open(statuses[i],O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd>=0){dprintf(fd,"%s: FAIL\nframework process did not start\n",i?"UIKIT":"COREANIMATION");close(fd);}
        framework_results[i]=mobile_run(frameworks[i],NULL,1,45);
    }
#if LILAC_MESA_PROBE
    fd=open("/private/var/run/lilac-mesa.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){dprintf(fd,"MESA SOFTWARE: FAIL\nprocess did not start\n");close(fd);}
    (void)mobile_run("/lilac-probe/bin/mesa_probe",NULL,1,90);
#endif
    printf("APPLE SERVICE/GRAPHICS BATCH: services=%s surfaces=%s display=%s coregraphics=%s CA=%s UIKit=%s\n",ok?"PASS":"FAIL",surfaces?"PASS":"FAIL",display?"PASS":"FAIL",graphics?"PASS":"FAIL",framework_results[0]?"PASS":"FAIL",framework_results[1]?"PASS":"FAIL");
    /* Keep the launchd job alive with a native surface and a visible heartbeat. */
    char *args[]={"/lilac-probe/bin/display_probe","--dashboard",NULL};
    char *env[]={"PATH=/bin:/usr/bin:/usr/sbin:/sbin","HOME=/var/root",NULL};
    execve(args[0],args,env);
    printf("DISPLAY dashboard exec failed errno=%d\n",errno);return 1;
}
static int mobile_show(const char *path,int tail) {
    int fd=open(path,O_RDONLY,0);if(fd<0)return 0;
    struct stat st;char buf[1601];
    if(tail && !fstat(fd,&st) && st.st_size>1600)lseek(fd,st.st_size-1600,0);
    ssize_t n=read(fd,buf,sizeof(buf)-1);close(fd);
    if(n>0){buf[n]=0;puts(buf);}return n>0;
}
static void mobile_monitor(void) {
    for(unsigned seconds=0;;seconds++) {
        if(seconds && seconds%30==0) {
            puts("\n=== LILAC MOBILE STATUS (observer, not PID1) ===");mobile_report();
            if(!mobile_show("/private/var/run/lilac-launchd-job",0))puts("Apple launchd job has not reported yet");
            if(!mobile_show("/private/var/run/lilac-services.status",0)) {
                puts("Service reply pending; launchd log tail:");mobile_show("/private/var/log/lilac-launchd.log",1);
            }
            mobile_show("/private/var/run/lilac-surface.status",0);
            mobile_show("/private/var/run/lilac-display.status",0);
            mobile_show("/private/var/run/lilac-graphics.status",0);
            mobile_show("/private/var/run/lilac-ca.status",0);
            mobile_show("/private/var/run/lilac-uikit.status",0);
#if LILAC_MESA_PROBE
            mobile_show("/private/var/run/lilac-mesa.status",0);
#endif
            printf("Observer alive seconds=%u; writable files stay in RAM\n",seconds);
        }
        usleep(1000000);
    }
}
static void mobile_test(int baseline_ok) {
    if(!baseline_ok){puts("MOBILE: baseline failed; keeping static PID1");return;}
    puts("\n=== LILAC MOBILE RUNTIME + APPLE LAUNCHD BATCH ===");
    if(mount("apfs","/ios/lilac-probe",MNT_RDONLY,(void *)"/dev/md0")) {
        printf("MOBILE RAM tools mount FAIL errno=%d\n",errno);return;
    }
    mobile_var_ok=mobile_var();if(!mobile_var_ok)return;
#ifndef LILAC_FAST_BOOT
    for(unsigned i=0;i<8;i++) {
        mobile_results[i]=mobile_run(i==7?"/lilac-probe/bin/foundation_probe":"/lilac-probe/bin/runtime_probe",i==7?NULL:mobile_names[i],0,30);
        mobile_passed+=mobile_results[i];mobile_total++;
    }
    mobile_report();
#endif
#if LILAC_DESKTOP_ATTEMPT
    /* Add just the original OS cryptex libraries, with the UFS /usr/lib below. */
    if(mount("bindfs","/ios/usr/lib",MNT_RDONLY|MNT_UNION,(void *)"/desktop-usr-lib")) {
        printf("DESKTOP cryptex library overlay FAIL errno=%d\n",errno);return;
    }
    struct stat libst;
    if(stat("/ios/usr/lib/libobjc-trampolines.dylib",&libst) ||
       stat("/ios/usr/lib/libffi-trampolines.dylib",&libst) ||
       stat("/ios/usr/lib/dyld",&libst)) {
        printf("DESKTOP cryptex overlay/fallback FAIL errno=%d\n",errno);return;
    }
    puts("DESKTOP cryptex libraries + UFS fallback: PASS");
#ifndef LILAC_FAST_BOOT
    (void)mobile_run("/lilac-probe/bin/desktop_libs",NULL,0,30);
#endif
    /* launchd jobs run as mobile; pre-create diagnostics on the RAM volume. */
    const char *desktop_logs[]={"/ios/private/var/log/lilac-runningboard.log",
        "/ios/private/var/log/lilac-backboard.log","/ios/private/var/log/lilac-springboard.log"};
    for(unsigned i=0;i<sizeof(desktop_logs)/sizeof(desktop_logs[0]);i++) {
        int log=open(desktop_logs[i],O_WRONLY|O_CREAT|O_TRUNC,0666);
        if(log<0 || chmod(desktop_logs[i],0666)) {
            if(log>=0)close(log);printf("DESKTOP log preparation FAIL errno=%d\n",errno);return;
        }
        close(log);
    }
#endif
#ifndef LILAC_FAST_BOOT
    int summary=open("/ios/private/var/run/lilac-runtime.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(summary>=0){dprintf(summary,"RUNTIME: %u/%u\nRAM var: %s\n",mobile_passed,mobile_total,mobile_var_ok?"PASS":"FAIL");close(summary);}
#endif
    /* The RAM overlay contains the explicitly selected jobs, not the full system. */
    const char *dest[]={"/ios/System/Library/LaunchDaemons","/ios/System/Library/NanoLaunchDaemons","/ios/System/Library/LaunchAngels","/ios/System/Library/xpc"};
    const char *source[]={"/LaunchDaemons","/empty","/empty","/empty"};
    for(unsigned i=0;i<4;i++)if(!mobile_dir(dest[i]) || mount("bindfs",dest[i],MNT_RDONLY,(void *)source[i])) {
        printf("MOBILE launchd overlay FAIL %s errno=%d; static PID1 retained\n",dest[i],errno);return;
    }
    if(!mobile_dir("/ios/private/preboot"))return;
    /* Remove the extra child devfs; root switching preserves the original /dev. */
    if(mini_syscall(159,(long)"/ios/dev",0,0,0,0,0)<0) {
        printf("MOBILE pre-pivot devfs unmount FAIL errno=%d\n",errno);return;
    }
    const char *pivot="/ios private/preboot";
    if(sysctlbyname("kern.hakios_switch_root",NULL,NULL,pivot,strlen(pivot)) || chdir("/")) {
        printf("MOBILE root switch FAIL errno=%d; static PID1 retained\n",errno);return;
    }
    puts("UFS SYSTEM ROOT: PASS; /private/var=RAM; /dev preserved");
    chmod("/dev/console",0666);
#ifndef LILAC_FAST_BOOT
    pid_t observer=fork();if(observer==0){mobile_monitor();_exit(0);}
    if(observer<0){puts("MOBILE observer fork FAIL; static PID1 retained");return;}
    puts("HANDING PID 1 TO ORIGINAL APPLE /sbin/launchd; observer remains");
#endif
    /* Keep verbose launchd output in RAM; the observer displays it if no job starts. */
#ifdef LILAC_FAST_BOOT
    int log=open("/dev/null",O_WRONLY,0);
#else
    int log=open("/private/var/log/lilac-launchd.log",O_WRONLY|O_CREAT|O_TRUNC,0644);
#endif
    if(log<0){printf("MOBILE launchd RAM log FAIL errno=%d\n",errno);return;}
    int console=mini_syscall(SYS_dup,2,0,0,0,0,0);
    if(console<0 || dup2(log,2)<0){close(log);return;}close(log);
    char *argv[]={"/sbin/launchd",NULL};
#ifdef LILAC_FAST_BOOT
    char *env[]={"PATH=/bin:/usr/bin:/usr/sbin:/sbin","HOME=/var/root","TMPDIR=/var/tmp",NULL};
#else
    char *env[]={"PATH=/bin:/usr/bin:/usr/sbin:/sbin","HOME=/var/root","TMPDIR=/var/tmp","OS_ACTIVITY_DT_MODE=YES",NULL};
#endif
    execve(argv[0],argv,env);
    dup2(console,2);close(console);printf("MOBILE Apple PID1 exec FAIL errno=%d\n",errno);
}
