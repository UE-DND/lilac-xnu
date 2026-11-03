/* SPDX-License-Identifier: BSD-2-Clause
 * Observe the original desktop jobs. A live process is not a working desktop.
 * Status and logs live solely on the RAM /var volume.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>

extern int proc_listpids(uint32_t, uint32_t, void *, int);
extern int proc_name(int, void *, uint32_t);

struct job { const char *name, *log; int pid; };
static struct job jobs[]={
    {"runningboardd","/private/var/log/lilac-runningboard.log",0},
    {"backboardd","/private/var/log/lilac-backboard.log",0},
    {"SpringBoard","/private/var/log/lilac-springboard.log",0},
};

/* Last printable nonempty line; the panel wraps it across screen rows. */
static void last_line(const char *path,char *out,size_t cap) {
    char buf[2049];out[0]=0;
    int fd=open(path,O_RDONLY);if(fd<0){snprintf(out,cap,"no log (errno %d)",errno);return;}
    off_t end=lseek(fd,0,SEEK_END);
    if(end>2048)lseek(fd,end-2048,SEEK_SET);else lseek(fd,0,SEEK_SET);
    ssize_t n=read(fd,buf,sizeof(buf)-1);close(fd);
    if(n<=0){snprintf(out,cap,"no log output");return;}buf[n]=0;
    while(n>0 && (buf[n-1]=='\n' || buf[n-1]=='\r' || buf[n-1]==' '))buf[--n]=0;
    ssize_t begin=n;while(begin>0 && buf[begin-1]!='\n' && buf[begin-1]!='\r')begin--;
    size_t j=0;for(ssize_t i=begin;i<n && j+1<cap;i++) {
        unsigned char c=(unsigned char)buf[i];out[j++]=(c>=32 && c<127)?(char)c:'?';
    }
    out[j]=0;if(!j)snprintf(out,cap,"no printable log");
}
static int sample(void) {
    int pids[2048];int bytes=proc_listpids(1,0,pids,sizeof(pids));
    for(unsigned j=0;j<sizeof(jobs)/sizeof(jobs[0]);j++)jobs[j].pid=0;
    /* A full buffer might omit a job; never report it as absent in that case. */
    if(bytes<0 || bytes>=(int)sizeof(pids))return 0;
    for(int i=0;i<bytes/(int)sizeof(pids[0]);i++) {
        char name[128]={0};if(pids[i]<=0 || proc_name(pids[i],name,sizeof(name))<=0)continue;
        for(unsigned j=0;j<sizeof(jobs)/sizeof(jobs[0]);j++)
            if(!strcmp(name,jobs[j].name))jobs[j].pid=pids[i];
    }
    return 1;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    for(unsigned elapsed=0;;elapsed+=5) {
        struct statfs fs;if(statfs("/private/var/run",&fs) || strcmp(fs.f_fstypename,"ramfs"))return 1;
        int valid=sample(),fd=open("/private/var/run/lilac-desktop.tmp",O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd>=0) {
            dprintf(fd,"DESKTOP ATTEMPT %us\n",elapsed);
            for(unsigned j=0;j<sizeof(jobs)/sizeof(jobs[0]);j++) {
                char line[181];last_line(jobs[j].log,line,sizeof(line));
                if(!valid)dprintf(fd,"%s: LIST ERROR\n",jobs[j].name);
                else if(jobs[j].pid)dprintf(fd,"%s: PID %d\n",jobs[j].name,jobs[j].pid);
                else dprintf(fd,"%s: NOT OBSERVED\n",jobs[j].name);
                dprintf(fd,"%.175s\n",line);
            }
            char libline[321];int libs=open("/private/var/run/lilac-cryptex.status",O_RDONLY);
            ssize_t libn=libs>=0?read(libs,libline,sizeof(libline)-1):-1;
            if(libs>=0)close(libs);
            if(libn>0){libline[libn]=0;dprintf(fd,"%s",libline);}
            else dprintf(fd,"cryptex: probe unavailable\n");
            char launchd[64];last_line("/private/var/log/lilac-launchd.log",launchd,sizeof(launchd));
            dprintf(fd,"launchd: %.60s\n",launchd);
            if(valid && jobs[1].pid && jobs[2].pid && elapsed>=120)dprintf(fd,"RELEASE DISPLAY\n");
            close(fd);rename("/private/var/run/lilac-desktop.tmp","/private/var/run/lilac-desktop.status");
        }
        sleep(5);
    }
}
