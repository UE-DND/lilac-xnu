#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char stored[512];
static unsigned reads, writes, fault;
static int fake_stat(const char *filename, struct stat *information)
{
    assert(!strcmp(filename, "/lilac-probe/lib/liblilac.touch.dylib"));
    information->st_mode = fault == 2 ? S_IFDIR : S_IFREG;
    return fault == 1 ? -1 : 0;
}
static int sysctlbyname(const char *name, void *old, size_t *length, const void *value, size_t bytes)
{
    assert(!strcmp(name, "kern.exec_inject"));
    if (value) {
        writes++;
        assert(bytes == strlen(value) + 1 && bytes < sizeof(stored));
        if (fault == 5) return -1;
        strcpy(stored, fault == 6 ? "incorrect" : value);
    } else {
        reads++;
        if (fault == 3 || (fault == 7 && reads == 2)) return -1;
        assert(*length == sizeof(stored));
        strcpy(old, stored);
        *length = fault == 4 ? 0 : strlen(stored) + 1;
        if (fault == 8) { memset(old, 'x', sizeof(stored)); *length = sizeof(stored); }
    }
    return 0;
}
#define stat(filename, information) fake_stat(filename, information)
#include "../../userland/touch-bridge.h"
#undef stat

int main(void)
{
    for (int bringup = 0; bringup <= 1; bringup++) {
        fault = 0;
        reads = writes = 0;
        stored[0] = 0;
        assert(lilac_touch_bridge_configure(bringup));
        assert(writes == 1 && reads == 2);
        assert(!strcmp(stored, bringup ? "/lilac-probe/lib/liblilac.touch.dylib backboardd SpringBoard" :
            "/lilac-probe/lib/liblilac.touch.dylib backboardd"));
        assert(lilac_touch_bridge_configure(bringup));
        assert(!lilac_touch_bridge_configure(!bringup));
        strcpy(stored, "/other/library other_program");
        writes = 0;
        assert(!lilac_touch_bridge_configure(bringup) && !writes);
        for (fault = 1; fault <= 8; fault++) {
            reads = writes = 0;
            stored[0] = 0;
            assert(!lilac_touch_bridge_configure(bringup));
            if (fault < 5 || fault == 8) assert(!writes);
        }
    }
    puts("PASS: touch bridge library, exact target, conflict refusal, sysctl errors and readback");
    return 0;
}
