/* SPDX-License-Identifier: BSD-2-Clause
 * Real iOS libSystem/dispatch/IPC checks. Each mode runs in its own bounded child.
 * All filesystem changes are restricted to the volatile /private/var ramfs.
 */
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/un.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

/* The SDK exports these through libSystem but does not ship public XPC headers. */
typedef void *xpc_object_t;
extern xpc_object_t xpc_dictionary_create(const char *const *, const xpc_object_t *, size_t);
extern void xpc_dictionary_set_string(xpc_object_t, const char *, const char *);
extern const char *xpc_dictionary_get_string(xpc_object_t, const char *);
extern void xpc_dictionary_set_int64(xpc_object_t, const char *, int64_t);
extern int64_t xpc_dictionary_get_int64(xpc_object_t, const char *);
extern xpc_object_t xpc_copy(xpc_object_t);
extern bool xpc_equal(xpc_object_t, xpc_object_t);
extern void xpc_release(xpc_object_t);

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned count;
static void *worker(void *arg) {
    for (unsigned i=0;i<10000;i++) {
        pthread_mutex_lock(&mutex);count++;pthread_mutex_unlock(&mutex);
    }
    return arg;
}
static int threads(void) {
    pthread_t t[4];unsigned made=0;int ok=1;
    for (;made<4;made++) if (pthread_create(&t[made],NULL,worker,(void *)(uintptr_t)(made+1)))break;
    for (unsigned i=0;i<made;i++) {void *r=NULL;if(pthread_join(t[i],&r)||r!=(void *)(uintptr_t)(i+1))ok=0;}
    return ok && made==4 && count==40000;
}
static int ipc(void) {
    mach_port_t p=MACH_PORT_NULL;struct {mach_msg_header_t h;uint32_t value;} sent={0};
    struct {mach_msg_header_t h;uint32_t value;mach_msg_trailer_t trailer;} got={0};
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&p))return 0;
    if(mach_port_insert_right(mach_task_self(),p,p,MACH_MSG_TYPE_MAKE_SEND))return 0;
    sent.h.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
    sent.h.msgh_size=sizeof(sent);sent.h.msgh_remote_port=p;sent.h.msgh_id=0x4c49;sent.value=0x1234abcd;
    int ok=mach_msg(&sent.h,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(sent),0,0,1000,0)==KERN_SUCCESS &&
        mach_msg(&got.h,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(got),p,1000,0)==KERN_SUCCESS &&
        got.value==sent.value && got.h.msgh_id==sent.h.msgh_id;
    mach_port_destroy(mach_task_self(),p);return ok;
}
static int events(void) {
    int f[2],q=kqueue();if(q<0 || pipe(f))return 0;
    struct kevent k,out;EV_SET(&k,f[0],EVFILT_READ,EV_ADD|EV_ONESHOT,0,0,NULL);
    struct timespec timeout={2,0};char c=0;
    int ok=kevent(q,&k,1,NULL,0,NULL)==0 && write(f[1],"K",1)==1 &&
        kevent(q,NULL,0,&out,1,&timeout)==1 && out.ident==(uintptr_t)f[0] &&
        read(f[0],&c,1)==1 && c=='K';
    close(f[0]);close(f[1]);close(q);return ok;
}
static int gcd(void) {
    dispatch_queue_t q=dispatch_queue_create("org.lilac.runtime",DISPATCH_QUEUE_CONCURRENT);
    dispatch_group_t g=dispatch_group_create();__block unsigned value=0;
    for(unsigned i=0;i<32;i++)dispatch_group_async(g,q,^{__sync_fetch_and_add(&value,1);});
    int ok=dispatch_group_wait(g,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0 && value==32;
    dispatch_semaphore_t s=dispatch_semaphore_create(0);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,50*NSEC_PER_MSEC),q,^{dispatch_semaphore_signal(s);});
    ok=dispatch_semaphore_wait(s,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0 && ok;
    dispatch_release(s);dispatch_release(g);dispatch_release(q);return ok;
}
static int sockets(void) {
    const char *path="/private/var/run/lilac-socket";
    int a=socket(AF_UNIX,SOCK_STREAM,0),b=-1,c=-1;struct sockaddr_un addr={0};
    addr.sun_family=AF_UNIX;strcpy(addr.sun_path,path);
    addr.sun_len=(uint8_t)(offsetof(struct sockaddr_un,sun_path)+strlen(path)+1);unlink(path);
    int ok=a>=0 && bind(a,(struct sockaddr *)&addr,addr.sun_len)==0 && listen(a,1)==0;
    if(ok) {b=socket(AF_UNIX,SOCK_STREAM,0);ok=b>=0 && connect(b,(struct sockaddr *)&addr,addr.sun_len)==0;}
    if(ok) {c=accept(a,NULL,NULL);char buf[4];ok=c>=0 && write(b,"UFS",4)==4 && read(c,buf,4)==4 && !memcmp(buf,"UFS",4);}
    if(c>=0)close(c);if(b>=0)close(b);if(a>=0)close(a);unlink(path);return ok;
}
static int local_xpc(void) {
    xpc_object_t d=xpc_dictionary_create(NULL,NULL,0);if(!d)return 0;
    xpc_dictionary_set_string(d,"source","original unsplit cache");xpc_dictionary_set_int64(d,"answer",42);
    xpc_object_t c=xpc_copy(d);const char *s=c?xpc_dictionary_get_string(c,"source"):NULL;
    int ok=c && xpc_equal(c,d) && s && !strcmp(s,"original unsplit cache") && xpc_dictionary_get_int64(c,"answer")==42;
    if(c)xpc_release(c);xpc_release(d);return ok;
}
static int files(void) {
    const char *path="/private/var/tmp/lilac-runtime.bin",*renamed="/private/var/tmp/lilac-runtime-moved.bin";
    struct statfs fs; /* Do not write unless the destination is actually the volatile filesystem. */
    if(statfs("/private/var/tmp",&fs)||strcmp(fs.f_fstypename,"ramfs"))return 0;
    unsigned char src[65573],readback[65573];for(unsigned i=0;i<sizeof(src);i++)src[i]=(i*31+(i>>8))&255;
    int fd=open(path,O_RDWR|O_CREAT|O_TRUNC,0600);if(fd<0)return 0;
    int ok=write(fd,src,sizeof(src))==sizeof(src) && pread(fd,readback,sizeof(src),0)==sizeof(src) && !memcmp(src,readback,sizeof(src));
    unsigned char *p=mmap(NULL,sizeof(src),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(p==MAP_FAILED)ok=0;
    else {p[16384]^=255;ok=msync(p,sizeof(src),MS_SYNC)==0 && ok;
        ok=pread(fd,readback,sizeof(src),0)==sizeof(src) && readback[16384]==(unsigned char)(src[16384]^255) && ok;
        munmap(p,sizeof(src));}
    close(fd);ok=rename(path,renamed)==0 && ok;unlink(path);unlink(renamed);return ok;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);if(argc!=2)return 64;
    printf("RUNTIME START %s pid=%d\n",argv[1],getpid());int ok=0;
    if(!strcmp(argv[1],"pthread"))ok=threads();
    else if(!strcmp(argv[1],"mach-ipc"))ok=ipc();
    else if(!strcmp(argv[1],"kqueue"))ok=events();
    else if(!strcmp(argv[1],"gcd"))ok=gcd();
    else if(!strcmp(argv[1],"unix-socket"))ok=sockets();
    else if(!strcmp(argv[1],"xpc-local"))ok=local_xpc();
    else if(!strcmp(argv[1],"ramfs"))ok=files();
    printf("RUNTIME %s: %s errno=%d\n",argv[1],ok?"PASS":"FAIL",errno);return ok?0:1;
}
