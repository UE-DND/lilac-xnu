/* SPDX-License-Identifier: GPL-2.0
 * IOSurface -> IOKit framebuffer -> Sony scanout, using CPU rendering only.
 * The font is the same GPL-2.0 bitmap table used by BootShim.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurfaceRef.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include "../kernel/lilac-display-api.h"
#include "../bootloader/font_8x16.h"

static io_connect_t connection;
static uint64_t info[LILAC_DISPLAY_STATUS_COUNT];
static unsigned logical_width(void) {return (uint32_t)info[5];}
static unsigned logical_height(void) {return info[5]>>32;}
static unsigned settled_count(void) {return (uint32_t)info[6];}
static unsigned filter_mode(void) {return info[6]>>32;}
static const uint32_t palette[9]={0xffef5350,0xff66bb6a,0xff42a5f5,
    0xff26c6da,0xffab47bc,0xffffca28,0xff78909c,0xffeeeeee,0xff101820};
static char grid_failure[96];

static int status(void) {
    uint32_t count=LILAC_DISPLAY_STATUS_COUNT;
    return IOConnectCallScalarMethod(connection,LILAC_DISPLAY_STATUS_SELECTOR,NULL,0,info,&count)==KERN_SUCCESS &&
        count==LILAC_DISPLAY_STATUS_COUNT && info[0]==LILAC_DISPLAY_API_VERSION &&
        info[2]>=6 && info[2]<=2048 && info[3]>=6 && info[3]<=4096 &&
        (info[4]==24 || info[4]==32) && logical_width()>0 && logical_width()<=8192 && logical_height()>0 && logical_height()<=8192;
}
static IOSurfaceRef surface(unsigned width,unsigned height) {
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    int values[]={width,height,4,0x42475241};
    const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
    for(unsigned i=0;i<4;i++) {CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);CFDictionarySetValue(d,keys[i],n);CFRelease(n);}
    IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);return s;
}
static int present(IOSurfaceRef s) {
    uint8_t request[0x46c]={0};uint64_t id=0;uint32_t count=1;
    if(IOConnectCallScalarMethod(connection,4,NULL,0,&id,&count)!=KERN_SUCCESS || count!=1)return 0;
    uint32_t word=(uint32_t)id;memcpy(request+0x50,&word,4);
    word=s?IOSurfaceGetID(s):0;memcpy(request+0x54,&word,4);
    word=1;memcpy(request+0xa4,&word,4);memcpy(request+0x104,&word,4);
    int32_t src[4]={0,0,s?(int32_t)IOSurfaceGetWidth(s):0,s?(int32_t)IOSurfaceGetHeight(s):0};
    int32_t dst[4]={0,0,(int32_t)logical_width(),(int32_t)logical_height()};
    memcpy(request+0x64,src,sizeof(src));memcpy(request+0xc4,dst,sizeof(dst));
    return IOConnectCallStructMethod(connection,5,request,sizeof(request),NULL,NULL)==KERN_SUCCESS;
}
static int grid(IOSurfaceRef s) {
    if(IOSurfaceLock(s,0,NULL)!=KERN_SUCCESS)return 0;
    unsigned w=IOSurfaceGetWidth(s),h=IOSurfaceGetHeight(s);void *base=IOSurfaceGetBaseAddress(s);
    if(!base){IOSurfaceUnlock(s,0,NULL);return 0;}
    for(unsigned y=0;y<h;y++) {
        uint32_t *row=(uint32_t *)((uint8_t *)base+y*IOSurfaceGetBytesPerRow(s));
        for(unsigned x=0;x<w;x++)row[x]=palette[(y*3/h)*3+x*3/w];
    }
    return IOSurfaceUnlock(s,0,NULL)==KERN_SUCCESS;
}
static int wait_grid(uint64_t before,uint64_t settled,int black,int need_settled) {
    int saw_frame=0,saw_settle=0;unsigned mismatch=0;uint32_t got=0,want=0;
    for(unsigned n=0;n<500;n++) {
        if(status() && info[1]!=before) {
            saw_frame=1;
            if(need_settled && settled_count()==settled) {usleep(10000);continue;}
            saw_settle=1;
            int ok=1;for(unsigned i=0;i<9;i++)if(info[7+i]!=(black?0:(palette[i]&0xffffff))) {
                if(ok){mismatch=i;got=(uint32_t)info[7+i];want=black?0:palette[i]&0xffffff;}ok=0;
            }
            if(ok)return 1;
        }
        usleep(10000);
    }
    if(!saw_frame)snprintf(grid_failure,sizeof(grid_failure),"no presented frame");
    else if(!saw_settle)snprintf(grid_failure,sizeof(grid_failure),"no filtered redraw");
    else snprintf(grid_failure,sizeof(grid_failure),"sample %u got %06x expected %06x",mismatch,got,want);
    return 0;
}
static int open_display(void) {
    io_service_t service=IOServiceGetMatchingService(0,IOServiceMatching("IOMobileFramebuffer"));
    if(!service){puts("DISPLAY: no IOMobileFramebuffer service");return 0;}
    kern_return_t r=IOServiceOpen(service,mach_task_self(),0,&connection);IOObjectRelease(service);
    if(r!=KERN_SUCCESS || !status()){printf("DISPLAY: open/status failed 0x%x\n",r);return 0;}
    printf("DISPLAY panel=%llux%llu depth=%llu logical=%ux%u\n",info[2],info[3],info[4],logical_width(),logical_height());return 1;
}
static void save(const char *path,const char *text) {
    struct statfs fs;if(statfs("/private/var/run",&fs)||strcmp(fs.f_fstypename,"ramfs"))return;
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0){write(fd,text,strlen(text));close(fd);}
}
static int check(void) {
    int scaled=0,filtered=0,native=0,clear=0;
    char detail[96]="";
    IOSurfaceRef small=surface(90,90),large=surface((unsigned)info[2],(unsigned)info[3]);
    if(small && large) {
        uint64_t before=info[1],settled=settled_count();
        scaled=grid(small) && present(small) && wait_grid(before,settled,0,0);
        if(!scaled)snprintf(detail,sizeof(detail),"scaled: %s",grid_failure[0]?grid_failure:"surface/swap failed");
        filtered=filter_mode()==0 ? 0 : wait_grid(before,settled,0,filter_mode()==2);
        if(!filtered && !detail[0])snprintf(detail,sizeof(detail),"filtered: %s",filter_mode()==0?"disabled":grid_failure);
        before=info[1];settled=settled_count();
        native=grid(large) && present(large) && wait_grid(before,settled,0,0);
        if(!native && !detail[0])snprintf(detail,sizeof(detail),"native: %s",grid_failure[0]?grid_failure:"surface/swap failed");
        before=info[1];clear=present(NULL) && wait_grid(before,settled_count(),1,0);
        if(!clear && !detail[0])snprintf(detail,sizeof(detail),"clear: %s",grid_failure[0]?grid_failure:"swap failed");
    }
    else snprintf(detail,sizeof(detail),"surface allocation failed");
    char text[256];int ok=scaled && filtered && native && clear;
    snprintf(text,sizeof(text),"DISPLAY: %s\nscaled=%s filtered=%s native=%s clear=%s\n%s\n",ok?"PASS":"FAIL",scaled?"PASS":"FAIL",filtered?"PASS":"FAIL",native?"PASS":"FAIL",clear?"PASS":"FAIL",detail);
    puts(text);save("/private/var/run/lilac-display.status",text);
    if(small)CFRelease(small);if(large)CFRelease(large);return ok?0:1;
}
static uint32_t *pixels;static size_t pitch;static unsigned width,height;
static void rectangle(unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color) {
    for(unsigned yy=y;yy<y+h && yy<height;yy++)for(unsigned xx=x;xx<x+w && xx<width;xx++)pixels[yy*pitch+xx]=color;
}
static void text(unsigned x,unsigned y,const char *s,unsigned scale,uint32_t color) {
    unsigned left=x;
    for(;*s;s++) {
        if(*s=='\n' || x+8*scale>width-24){x=left;y+=18*scale;if(*s=='\n')continue;}
        unsigned c=(unsigned char)*s;if(c<32 || c>126)c='?';
        for(unsigned row=0;row<16;row++)for(unsigned col=0;col<8;col++)if(font_8x16[c-32][row]&(0x80>>col))
            rectangle(x+col*scale,y+row*scale,scale,scale,color);
        x+=8*scale;
    }
}
static void status_text(unsigned y,const char *path,const char *fallback) {
    char s[240];int fd=open(path,O_RDONLY);ssize_t n=fd>=0?read(fd,s,sizeof(s)-1):-1;
    if(fd>=0)close(fd);
    if(n>0)s[n]=0;else {snprintf(s,sizeof(s),"%s",fallback);}
    text(32,y,s,2,strstr(s,"FAIL")?0xffef5350:0xff80e6a0);
}
static int dashboard(void) {
    width=(unsigned)info[2];height=(unsigned)info[3];
    IOSurfaceRef s=surface(width,height);if(!s)return 1;
    for(unsigned tick=0;;tick++) {
        if(IOSurfaceLock(s,0,NULL)!=KERN_SUCCESS)break;
        pixels=IOSurfaceGetBaseAddress(s);pitch=IOSurfaceGetBytesPerRow(s)/4;
        if(!pixels){IOSurfaceUnlock(s,0,NULL);break;}
        rectangle(0,0,width,height,0xff101c2c);
        text(32,40,"LILAC DARWIN",3,0xff5bd8ff);
        text(32,112,"IOSurface -> IOKit -> Sony",2,0xffeeeeee);
        for(unsigned i=0;i<9;i++)rectangle(i*width/9,190,width/9+1,24,palette[i]);
        status_text(235,"/private/var/run/lilac-runtime.status","Runtime summary pending");
        status_text(325,"/private/var/run/lilac-services.status","Services pending");
        status_text(430,"/private/var/run/lilac-surface.status","IOSurface checks pending");
        status_text(505,"/private/var/run/lilac-display.status","Display checks pending");
#if LILAC_DESKTOP_ATTEMPT
        char desktop[1200];int dfd=open("/private/var/run/lilac-desktop.status",O_RDONLY);
        ssize_t dn=dfd>=0?read(dfd,desktop,sizeof(desktop)-1):-1;
        if(dfd>=0)close(dfd);
        if(dn>0)desktop[dn]=0;else snprintf(desktop,sizeof(desktop),"DESKTOP ATTEMPT: observer pending");
        text(32,620,desktop,1,0xffeeeeee);
        if(tick>=40 && strstr(desktop,"RELEASE DISPLAY")) {
            IOSurfaceUnlock(s,0,NULL);
            puts("DESKTOP processes live: releasing diagnostic scanout; desktop output not yet verified");
            CFRelease(s);return 0;
        }
#else
        status_text(620,"/private/var/run/lilac-graphics.status","Apple graphics pending");
        status_text(715,"/private/var/run/lilac-ca.status","CoreAnimation pending");
        status_text(810,"/private/var/run/lilac-uikit.status","UIKit pending");
        status_text(915,"/private/var/run/lilac-mesa.status","Mesa not included");
#endif
        text(32,1010,"UFS: READ ONLY  VAR: RAM",2,0xffd5deeb);
        char line[100];snprintf(line,sizeof(line),"Live tick %u / shown %llu",tick,info[1]);text(32,1080,line,2,0xff5bd8ff);
        rectangle(32+(tick%20)*30,1150,22,22,0xff80e6a0);
        IOSurfaceUnlock(s,0,NULL);
        if(!present(s))break;
        usleep(1000000);status();
    }
    CFRelease(s);return 1;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(!open_display())return 1;
    int r=argc==2 && !strcmp(argv[1],"--dashboard")?dashboard():check();
    IOServiceClose(connection);return r;
}
