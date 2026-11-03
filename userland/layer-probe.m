/* SPDX-License-Identifier: BSD-2-Clause
 * Render an actual CoreAnimation layer tree or UIKit view tree offscreen.
 */
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>
#if LILAC_UIKIT
#import <UIKit/UIKit.h>
#endif
#include <sys/mount.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "graphics-present.h"

#if LILAC_UIKIT
#define TITLE "UIKIT"
#define STATUS_PATH "/private/var/run/lilac-uikit.status"
#else
#define TITLE "COREANIMATION"
#define STATUS_PATH "/private/var/run/lilac-ca.status"
#endif
static void report(int scene,int detail,int displayed,const char *stage) {
    char out[256];struct statfs fs;
    snprintf(out,sizeof(out),TITLE ": %s\nscene=%s detail=%s display=%s\n%s\n",
        scene&&detail&&displayed?"PASS":"FAIL",scene?"PASS":"FAIL",detail?"PASS":"FAIL",displayed?"PASS":"FAIL",stage);
    puts(out);
    if(statfs("/private/var/run",&fs)||strcmp(fs.f_fstypename,"ramfs"))return;
    int fd=open(STATUS_PATH,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd>=0){(void)write(fd,out,strlen(out));close(fd);}
}
static uint32_t pixel(const uint8_t *p,size_t row,unsigned x,unsigned y) {
    uint32_t v;memcpy(&v,p+(size_t)y*row+x*4,4);return v;
}
static int render_scene(IOSurfaceRef surface,int *detail) {
    *detail=0;int scene=0;
    if(IOSurfaceLock(surface,0,NULL)!=KERN_SUCCESS)return 0;
    uint8_t *p=IOSurfaceGetBaseAddress(surface);size_t row=IOSurfaceGetBytesPerRow(surface);
    CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
    CGContextRef c=p&&cs?CGBitmapContextCreate(p,360,360,8,row,cs,
        kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little):NULL;
    if(c) {
        [CATransaction begin];[CATransaction setDisableActions:YES];
#if LILAC_UIKIT
        UIView *root=[[UIView alloc] initWithFrame:CGRectMake(0,0,360,360)];
        UIColor *colors[]={UIColor.redColor,UIColor.greenColor,UIColor.blueColor};
        for(unsigned i=0;i<3;i++) {
            UIView *child=[[UIView alloc] initWithFrame:CGRectMake(120*i,0,120,360)];
            child.backgroundColor=colors[i];[root addSubview:child];
        }
        UILabel *label=[[UILabel alloc] initWithFrame:CGRectMake(8,8,105,40)];
        label.text=@"Darwin";label.textColor=UIColor.whiteColor;label.font=[UIFont fontWithName:@"CourierNewPSMT" size:20];
        label.backgroundColor=UIColor.clearColor;[root addSubview:label];
        [root setNeedsLayout];[root layoutIfNeeded];[label.layer setNeedsDisplay];[label.layer displayIfNeeded];
        [CATransaction commit];
        [root.layer renderInContext:c];
#else
        CALayer *root=[CALayer layer];root.frame=CGRectMake(0,0,360,360);
        CGFloat components[3][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1}};
        for(unsigned i=0;i<3;i++) {
            CALayer *child=[CALayer layer];child.frame=CGRectMake(120*i,0,120,360);
            CGColorRef color=CGColorCreate(cs,components[i]);child.backgroundColor=color;CGColorRelease(color);
            [root addSublayer:child];
        }
        CGFloat white[]={1,1,1,1};CGColorRef color=CGColorCreate(cs,white);
        CALayer *overlay=[CALayer layer];overlay.frame=CGRectMake(0,0,32,32);overlay.backgroundColor=color;
        CGColorRelease(color);overlay.opacity=0.5;[root addSublayer:overlay];
        [CATransaction commit];
        [root renderInContext:c];
#endif
        CGContextFlush(c);
        uint32_t expected[]={0xffff0000,0xff00ff00,0xff0000ff};scene=1;
        for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++)if(pixel(p,row,60+x*120,60+y*120)!=expected[x])scene=0;
#if LILAC_UIKIT
        unsigned ink=0;
        /* UIKit and CoreGraphics differ in row origin; inspect both edge bands. */
        for(unsigned y=8;y<48;y++)for(unsigned x=8;x<113;x++) {
            uint32_t a=pixel(p,row,x,y),b=pixel(p,row,x,359-y);
            ink+=((a&0x00ffff)>0x8080 && ((a>>8)&255)>128 && (a&255)>128);
            ink+=((b&0x00ffff)>0x8080 && ((b>>8)&255)>128 && (b&255)>128);
        }
        *detail=label.font!=nil && [label.text isEqualToString:@"Darwin"] && ink>20;
        printf("UIKIT label pixels=%u\n",ink);
#else
        uint32_t a=pixel(p,row,16,16),b=pixel(p,row,16,343);
        uint32_t mixed=a!=expected[0]?a:b;
        *detail=(mixed&0xffff0000)==0xffff0000 && ((mixed>>8)&255)>=115 && (mixed&255)>=115;
        /* A second transaction changes the tree; verify both old and new pixels. */
        [CATransaction begin];[CATransaction setDisableActions:YES];
        overlay.frame=CGRectMake(32,0,32,32);[CATransaction commit];
        [root renderInContext:c];CGContextFlush(c);
        a=pixel(p,row,48,16);b=pixel(p,row,48,343);mixed=a!=expected[0]?a:b;
        *detail=*detail && (mixed&0xffff0000)==0xffff0000 && ((mixed>>8)&255)>=115 && (mixed&255)>=115 &&
            pixel(p,row,16,16)==expected[0] && pixel(p,row,16,343)==expected[0];
#endif
        CGContextRelease(c);
    }
    if(cs)CGColorSpaceRelease(cs);
    if(IOSurfaceUnlock(surface,0,NULL)!=KERN_SUCCESS)scene=0;
    return scene;
}
int main(void) {
    @autoreleasepool {
        setvbuf(stdout,NULL,_IONBF,0);report(0,0,0,"starting framework");
        IOSurfaceRef surface=make_surface();if(!surface){report(0,0,0,"surface failed");return 1;}
        int detail=0,scene=render_scene(surface,&detail);
        report(scene,detail,0,"offscreen rendered");
        int displayed=scene && scanout(surface);
        report(scene,detail,displayed,displayed?"":"render or display failed");
        CFRelease(surface);return scene&&detail&&displayed?0:1;
    }
}
