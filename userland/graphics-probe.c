/* SPDX-License-Identifier: BSD-2-Clause
 * Exercise Apple's CoreGraphics/CoreText with the actual IOSurface scanout path.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <IOSurface/IOSurfaceRef.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "graphics-present.h"

#ifndef LILAC_GRAPHICS_FONT
#define LILAC_GRAPHICS_FONT "/System/Library/Fonts/Core/CourierNew.ttf"
#endif
static void save(int bitmap,int text,int scanout,const char *stage) {
    char s[256]; struct statfs fs;
    snprintf(s,sizeof(s),"APPLE GRAPHICS: %s\nCG=%s TEXT=%s SCANOUT=%s\n%s\n",
        bitmap&&text&&scanout?"PASS":"FAIL",bitmap?"PASS":"FAIL",text?"PASS":"FAIL",scanout?"PASS":"FAIL",stage);
    puts(s);
    if(statfs("/private/var/run",&fs) || strcmp(fs.f_fstypename,"ramfs"))return;
    int fd=open("/private/var/run/lilac-graphics.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){(void)write(fd,s,strlen(s));close(fd);}
}
static int bitmap_draw(IOSurfaceRef s) {
    int ok=0;
    if(IOSurfaceLock(s,0,NULL)!=KERN_SUCCESS)return 0;
    unsigned char *p=IOSurfaceGetBaseAddress(s);size_t pitch=IOSurfaceGetBytesPerRow(s);
    CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGContextRef c=p && cs?CGBitmapContextCreate(p,360,360,8,pitch,cs,
        kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little):NULL;
    if(c) {
        CGContextSetShouldAntialias(c,false);
        CGContextSetRGBFillColor(c,1,0,0,1);CGContextFillRect(c,CGRectMake(0,0,120,360));
        CGContextSetRGBFillColor(c,0,1,0,1);CGContextFillRect(c,CGRectMake(120,0,120,360));
        CGContextSetRGBFillColor(c,0,0,1,1);CGContextFillRect(c,CGRectMake(240,0,120,360));
        /* Exercise alpha composition separately from the nine sampled centers. */
        CGContextSetRGBFillColor(c,1,1,1,0.5);CGContextFillRect(c,CGRectMake(0,0,32,32));
        CGContextFlush(c);
        uint32_t colors[]={0xffff0000,0xff00ff00,0xff0000ff};ok=1;
        for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++) {
            uint32_t pixel;memcpy(&pixel,p+(60+y*120)*pitch+(60+x*120)*4,4);
            if(pixel!=colors[x])ok=0;
        }
        /* CGContext's lower-left corner lands at the bottom of bitmap memory. */
        uint32_t a,b;memcpy(&a,p+16*pitch+16*4,4);memcpy(&b,p+343*pitch+16*4,4);
        uint32_t mixed=a!=colors[0]?a:b;
        if((mixed&0xffff0000)!=0xffff0000 || ((mixed>>8)&255)<120 || (mixed&255)<120)ok=0;
        CGContextRelease(c);
    }
    if(cs)CGColorSpaceRelease(cs);
    if(IOSurfaceUnlock(s,0,NULL)!=KERN_SUCCESS)ok=0;
    return ok;
}
static int vector_draw(void) {
    uint32_t p[64*64]={0};int ok=0;
    CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGContextRef c=cs?CGBitmapContextCreate(p,64,64,8,64*4,cs,
        kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little):NULL;
    if(c) {
        CGContextSetShouldAntialias(c,false);
        CGContextSetRGBFillColor(c,0,0,0,1);CGContextFillRect(c,CGRectMake(0,0,64,64));
        CGContextClipToRect(c,CGRectMake(8,8,24,24));CGContextTranslateCTM(c,4,4);
        CGContextBeginPath(c);CGContextMoveToPoint(c,0,0);CGContextAddLineToPoint(c,40,0);
        CGContextAddLineToPoint(c,40,40);CGContextAddLineToPoint(c,0,40);CGContextClosePath(c);
        CGContextSetRGBFillColor(c,1,0,0,1);CGContextFillPath(c);CGContextFlush(c);
        uint32_t a=p[16*64+16],b=p[47*64+16];
        ok=((a==0xffff0000 && b==0xff000000)||(b==0xffff0000 && a==0xff000000)) &&
            p[16*64+40]==0xff000000 && p[47*64+40]==0xff000000;
        CGContextRelease(c);
    }
    if(cs)CGColorSpaceRelease(cs);return ok;
}
static int text_draw(void) {
    enum { W=512,H=96 };static unsigned char pixels[W*H*4];int ok=0;
    CGDataProviderRef provider=CGDataProviderCreateWithFilename(LILAC_GRAPHICS_FONT);
    CGFontRef cg=provider?CGFontCreateWithDataProvider(provider):NULL;
    CTFontRef font=cg?CTFontCreateWithGraphicsFont(cg,40,NULL,NULL):NULL;
    CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGContextRef context=cs?CGBitmapContextCreate(pixels,W,H,8,W*4,cs,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little):NULL;
    CFStringRef string=CFSTR("Darwin 123");
    const void *keys[]={kCTFontAttributeName};const void *values[]={font};
    CFDictionaryRef attr=font?CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks):NULL;
    CFAttributedStringRef attributed=attr?CFAttributedStringCreate(NULL,string,attr):NULL;
    CTLineRef line=attributed?CTLineCreateWithAttributedString(attributed):NULL;
    if(line && context) {
        CGContextSetRGBFillColor(context,1,1,1,1);CGContextSetTextPosition(context,8,24);
        CTLineDraw(line,context);CGContextFlush(context);
        unsigned painted=0;for(unsigned i=0;i<W*H;i++)painted+=pixels[i*4+3]>32;
        ok=CTLineGetGlyphCount(line)>0 && painted>30 && painted<W*H/2;
        printf("CORETEXT glyphs=%ld painted=%u\n",(long)CTLineGetGlyphCount(line),painted);
    }
    if(line)CFRelease(line);if(attributed)CFRelease(attributed);if(attr)CFRelease(attr);
    if(context)CGContextRelease(context);if(cs)CGColorSpaceRelease(cs);
    if(font)CFRelease(font);if(cg)CGFontRelease(cg);if(provider)CGDataProviderRelease(provider);
    return ok;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);save(0,0,0,"starting framework test");
    IOSurfaceRef s=make_surface();if(!s){save(0,0,0,"IOSurface create failed");return 1;}
    int bitmap=bitmap_draw(s) && vector_draw();save(bitmap,0,0,"CoreGraphics bitmap/vector complete");
    int text=text_draw();save(bitmap,text,0,"CoreText font and glyphs complete");
    int shown=bitmap && scanout(s);save(bitmap,text,shown,shown?"":"scanout or framework check failed");
    CFRelease(s);return bitmap&&text&&shown?0:1;
}
