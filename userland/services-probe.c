/* SPDX-License-Identifier: BSD-2-Clause
 * This client is launched by Apple's launchd, with its real bootstrap port.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <notify.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
extern kern_return_t bootstrap_look_up(mach_port_t,const char *,mach_port_t *);
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);printf("APPLE SERVICES START pid=%d ppid=%d\n",getpid(),getppid());
    mach_port_t bootstrap=MACH_PORT_NULL,notify=MACH_PORT_NULL,prefs=MACH_PORT_NULL;
    int boot=task_get_bootstrap_port(mach_task_self(),&bootstrap)==KERN_SUCCESS && bootstrap!=MACH_PORT_NULL;
    int nlookup=boot && bootstrap_look_up(bootstrap,"com.apple.system.notification_center",&notify)==KERN_SUCCESS;
    int plookup=boot && bootstrap_look_up(bootstrap,"com.apple.cfprefsd.daemon",&prefs)==KERN_SUCCESS;
    printf("BOOTSTRAP notifyd=%s cfprefsd=%s\n",nlookup?"PASS":"FAIL",plookup?"PASS":"FAIL");
    int token=-1,changed=0;uint64_t state=0;
    int n=nlookup && notify_register_check("org.lilac.port.test",&token)==NOTIFY_STATUS_OK;
    if(n) {
        notify_check(token,&changed);
        n=notify_set_state(token,0x1234abcd)==NOTIFY_STATUS_OK && notify_get_state(token,&state)==NOTIFY_STATUS_OK && state==0x1234abcd;
        changed=0;n=notify_post("org.lilac.port.test")==NOTIFY_STATUS_OK && n;usleep(200000);
        n=notify_check(token,&changed)==NOTIFY_STATUS_OK && changed && n;notify_cancel(token);
    }
    printf("APPLE notifyd state/post/check: %s\n",n?"PASS":"FAIL");
    struct statfs fs;int p=0;
    if(plookup && statfs("/private/var/root/Library/Preferences",&fs)==0 && !strcmp(fs.f_fstypename,"ramfs")) {
        CFStringRef app=CFSTR("org.lilac.port"),key=CFSTR("message");
        CFPreferencesSetAppValue(key,CFSTR("preferences on RAM; system on UFS"),app);
        p=CFPreferencesAppSynchronize(app);
        CFPropertyListRef value=CFPreferencesCopyAppValue(key,app);
        p=value && CFEqual(value,CFSTR("preferences on RAM; system on UFS")) && p;
        if(value)CFRelease(value);
    }
    printf("APPLE cfprefsd sync/readback: %s\n",p?"PASS":"FAIL");
    char report[180];int len=snprintf(report,sizeof(report),"APPLE SERVICES: bootstrap=%s notifyd=%s cfprefsd=%s\n",boot?"PASS":"FAIL",n?"PASS":"FAIL",p?"PASS":"FAIL");
    if(statfs("/private/var/run",&fs)==0 && !strcmp(fs.f_fstypename,"ramfs")) {
        int fd=open("/private/var/run/lilac-services.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd>=0){write(fd,report,len);close(fd);}
    }
    puts(report);return boot && n && p?0:1;
}
