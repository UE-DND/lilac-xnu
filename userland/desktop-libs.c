/* SPDX-License-Identifier: BSD-2-Clause
 * Check the two original OS-cryptex trampoline libraries through dyld.
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>

int main(void) {
    struct statfs fs;
    if(statfs("/private/var/run",&fs) || strcmp(fs.f_fstypename,"ramfs"))return 1;
    int fd=open("/private/var/run/lilac-cryptex.status",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0)return 1;
    const char *names[]={"/usr/lib/libobjc-trampolines.dylib","/usr/lib/libffi-trampolines.dylib"};
    int ok=1;
    for(unsigned i=0;i<2;i++) {
        void *handle=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);
        const char *error=handle?NULL:dlerror();
        dprintf(fd,"%s: %s%s%s\n",i?"FFI":"OBJC",handle?"LOADED":"LOAD FAILED",
                error?" ":"",error?error:"");
        if(!handle)ok=0;else dlclose(handle);
    }
    close(fd);return ok?0:1;
}
