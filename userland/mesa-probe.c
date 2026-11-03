/* SPDX-License-Identifier: BSD-2-Clause
 * Load the cross-built Mesa library from RAM and exercise its software renderer.
 */
#include <dlfcn.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#define LIBRARY "/private/var/tmp/lilac-libOSMesa.8.dylib"
static void report(int ok,const char *detail) {
    struct statfs fs;char s[256];snprintf(s,sizeof(s),"MESA SOFTWARE: %s\n%.64s\n",ok?"PASS":"FAIL",detail);puts(s);
    if(statfs("/private/var/run",&fs)||strcmp(fs.f_fstypename,"ramfs"))return;
    int fd=open("/private/var/run/lilac-mesa.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){(void)write(fd,s,strlen(s));close(fd);}
}
static int unpack(void) {
    struct statfs fs;if(statfs("/private/var/tmp",&fs)||strcmp(fs.f_fstypename,"ramfs"))return 0;
    gzFile source=gzopen("/lilac-probe/probe/libOSMesa.8.dylib.gz","rb");if(!source)return 0;
    int fd=open(LIBRARY,O_WRONLY|O_CREAT|O_TRUNC,0755),ok=fd>=0;size_t total=0;unsigned char buf[16384];
    while(ok) {
        int n=gzread(source,buf,sizeof(buf));if(n==0)break;if(n<0 || total+(size_t)n>32*1024*1024){ok=0;break;}
        for(int at=0;at<n;) {ssize_t w=write(fd,buf+at,(size_t)(n-at));if(w<=0){ok=0;break;}at+=(int)w;}
        total+=(size_t)n;
    }
    if(gzclose(source)!=Z_OK)ok=0;
    if(fd>=0 && close(fd))ok=0;
    if(!ok || total<1024){unlink(LIBRARY);return 0;}return 1;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);report(0,"unpacking into RAM");
    if(!unpack()){report(0,"library unpack failed");return 1;}
    setenv("OSMESA_SOFTWARE","1",1);setenv("GALLIUM_DRIVER","softpipe",1);
    report(0,"loading Mesa dylib");void *lib=dlopen(LIBRARY,RTLD_NOW|RTLD_LOCAL);
    if(!lib){const char *e=dlerror();report(0,e?e:"dlopen failed");return 1;}
    void *(*create)(const int *,void *)=dlsym(lib,"OSMesaCreateContextAttribs");
    unsigned char (*current)(void *,void *,unsigned,int,int)=dlsym(lib,"OSMesaMakeCurrent");
    void (*destroy)(void *)=dlsym(lib,"OSMesaDestroyContext");
    void (*clearColor)(float,float,float,float)=dlsym(lib,"glClearColor");
    void (*clear)(unsigned)=dlsym(lib,"glClear");
    void (*finish)(void)=dlsym(lib,"glFinish");
    unsigned (*error)(void)=dlsym(lib,"glGetError");
    const unsigned char *(*getString)(unsigned)=dlsym(lib,"glGetString");
    if(!create || !current || !destroy || !clearColor || !clear || !finish || !error || !getString) {report(0,"Mesa symbols missing");dlclose(lib);return 1;}
    const int attrs[]={0x22,0x1908,0x30,24,0x31,8,0x33,0x38,0x36,2,0x37,0,0};
    unsigned char pixels[16*16*4]={0};report(0,"creating software GL context");
    void *ctx=create(attrs,NULL);
    if(!ctx || !current(ctx,pixels,0x1401,16,16)){report(0,"context creation failed");if(ctx)destroy(ctx);dlclose(lib);return 1;}
    const unsigned char *renderer=getString(0x1f01);char desc[160];
    snprintf(desc,sizeof(desc),"renderer: %.100s",renderer?(const char *)renderer:"unknown");
    clearColor(1,0,0,1);clear(0x4000);finish();int ok=error()==0;
    for(unsigned i=0;i<16*16;i++)if(pixels[4*i]!=255 || pixels[4*i+1]!=0 || pixels[4*i+2]!=0 || pixels[4*i+3]!=255)ok=0;
    if(!renderer || !strstr((const char *)renderer,"softpipe"))ok=0;
    report(ok,desc);destroy(ctx);dlclose(lib);return ok?0:1;
}
