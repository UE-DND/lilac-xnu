/* SPDX-License-Identifier: BSD-2-Clause
 * Static Linux/Android arm64 helper. Opens one caller-owned regular image
 * read-only, syncs it, and prints FIEMAP. Never opens or writes a block device.
 */
#include <stdint.h>
#include <stddef.h>
static long call6(long n,long a,long b,long c,long d,long e,long f) {
    register long x8 __asm__("x8")=n;
    register long x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c;
    register long x3 __asm__("x3")=d,x4 __asm__("x4")=e,x5 __asm__("x5")=f;
    __asm__ volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5):"memory");return x0;
}
static void out(const char *p) {
    size_t n=0;while(p[n])n++;
    while(n){long r=call6(64,1,(long)p,n,0,0,0);if(r<=0)break;p+=r;n-=(size_t)r;}
}
static void number(uint64_t value) {char s[24];unsigned i=23;s[i]=0;do{s[--i]=(char)('0'+value%10);value/=10;}while(value);out(s+i);}
static int fail(const char *at,long error){out("{\"error\":\"");out(at);out("\",\"errno\":");number((uint64_t)-error);out("}\n");return 1;}
struct extent {uint64_t logical,physical,length,reserved64[2];uint32_t flags,reserved[3];};
struct fiemap {uint64_t start,length;uint32_t flags,mapped,count,reserved;struct extent extent[4096];};
static struct fiemap map;
int main(int argc,char **argv) {
    if(argc!=2)return fail("usage: android-fiemap IMAGE",-22);
    long fd=call6(56,-100,(long)argv[1],0,0,0,0);if(fd<0)return fail("open",fd);
    long size=call6(62,fd,0,2,0,0,0);if(size<=0)return fail("size",size?size:-22);
    uint64_t flags=0;long r=call6(29,fd,0x80086601,(long)&flags,0,0,0);if(r<0)return fail("GETFLAGS",r);
    map.length=~0ULL;map.flags=1;map.count=4096;
    r=call6(29,fd,0xc020660b,(long)&map,0,0,0);if(r<0)return fail("FIEMAP",r);
    out("{\"size\":");number((uint64_t)size);out(",\"inode_flags\":");number(flags);
    out(",\"mapped\":");number(map.mapped);out(",\"extents\":[");
    if(map.mapped>4096)return fail("mapped_count",-75);
    for(uint32_t i=0;i<map.mapped;i++){
        struct extent *e=&map.extent[i];if(i)out(",");out("{\"logical\":");number(e->logical);
        out(",\"physical\":");number(e->physical);out(",\"length\":");number(e->length);
        out(",\"flags\":");number(e->flags);out("}");
    }
    out("]}\n");call6(57,fd,0,0,0,0,0);return 0;
}
__asm__(".globl _start\n_start:\nmov x0,sp\nldr x0,[x0]\nadd x1,sp,#8\nand sp,x1,#-16\nbl main\nmov x8,#93\nsvc #0\n");
