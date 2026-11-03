/* SPDX-License-Identifier: BSD-2-Clause
 * Shared IOSurface and fixed-sample framebuffer presentation for graphics probes.
 */
#ifndef LILAC_GRAPHICS_PRESENT_H
#define LILAC_GRAPHICS_PRESENT_H
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "../kernel/lilac-display-api.h"

static IOSurfaceRef make_surface(void) {
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!d)return NULL;
    int v[]={360,360,4,0x42475241};
    const void *k[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
    for(unsigned i=0;i<4;i++) {
        CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&v[i]);
        if(!n){CFRelease(d);return NULL;}
        CFDictionarySetValue(d,k[i],n);CFRelease(n);
    }
    IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);return s;
}
static int scanout(IOSurfaceRef s) {
    io_service_t service=IOServiceGetMatchingService(0,IOServiceMatching("IOMobileFramebuffer"));
    if(!service)return 0;
    io_connect_t con=0;kern_return_t r=IOServiceOpen(service,mach_task_self(),0,&con);IOObjectRelease(service);
    if(r!=KERN_SUCCESS)return 0;
    uint64_t status[LILAC_DISPLAY_STATUS_COUNT]={0};uint32_t n=LILAC_DISPLAY_STATUS_COUNT;
    int ok=0;
    if(IOConnectCallScalarMethod(con,LILAC_DISPLAY_STATUS_SELECTOR,NULL,0,status,&n)!=KERN_SUCCESS ||
       n!=LILAC_DISPLAY_STATUS_COUNT || status[0]!=LILAC_DISPLAY_API_VERSION)goto done;
    uint64_t before=status[1],id=0;n=1;
    if(IOConnectCallScalarMethod(con,4,NULL,0,&id,&n)!=KERN_SUCCESS || n!=1)goto done;
    uint8_t request[0x46c]={0};uint32_t v=(uint32_t)id;memcpy(request+0x50,&v,4);
    v=IOSurfaceGetID(s);memcpy(request+0x54,&v,4);v=1;memcpy(request+0xa4,&v,4);memcpy(request+0x104,&v,4);
    int32_t src[]={0,0,360,360};int32_t dst[]={0,0,(int32_t)(uint32_t)status[5],(int32_t)(status[5]>>32)};
    if(dst[2]<=0 || dst[3]<=0)goto done;
    memcpy(request+0x64,src,sizeof(src));memcpy(request+0xc4,dst,sizeof(dst));
    if(IOConnectCallStructMethod(con,5,request,sizeof(request),NULL,NULL)!=KERN_SUCCESS)goto done;
    for(unsigned i=0;i<500;i++) {
        n=LILAC_DISPLAY_STATUS_COUNT;
        if(IOConnectCallScalarMethod(con,LILAC_DISPLAY_STATUS_SELECTOR,NULL,0,status,&n)==KERN_SUCCESS &&
           n==LILAC_DISPLAY_STATUS_COUNT && status[1]!=before) {
            uint32_t want[]={0xff0000,0x00ff00,0x0000ff};ok=1;
            for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++)if(status[7+y*3+x]!=want[x])ok=0;
            if(ok)break;
        }
        usleep(10000);
    }
done:
    IOServiceClose(con);return ok;
}
#endif
