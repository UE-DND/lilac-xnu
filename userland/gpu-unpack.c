/* SPDX-License-Identifier: BSD-2-Clause
 * Expand the prepared Mesa library to the volatile data volume before launchd.
 */
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#define SOURCE "/lilac-probe/probe/gpu-mesa.gz"
#define DIRECTORY "/private/var/g"
#define TEMP DIRECTORY "/mesa.tmp"
#define TARGET DIRECTORY "/mesa.dylib"

int main(void)
{
    struct statfs fs;
    if (statfs("/private/var", &fs) || strcmp(fs.f_fstypename, "ramfs")) return 1;
    if (mkdir(DIRECTORY, 0755)) return 2;
    gzFile in = gzopen(SOURCE, "rb");
    if (!in) return 3;
    int fd = open(TEMP, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0755);
    int ok = fd >= 0;
    uint64_t total = 0;
    unsigned char buffer[16384];
    while (ok) {
        int n = gzread(in, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0 || total + (uint64_t)n > LILAC_GPU_MESA_BYTES) { ok = 0; break; }
        for (int at = 0; at < n;) {
            ssize_t written = write(fd, buffer + at, (size_t)(n - at));
            if (written <= 0) { ok = 0; break; }
            at += (int)written;
        }
        total += (uint64_t)n;
    }
    if (gzclose(in) != Z_OK || total != LILAC_GPU_MESA_BYTES) ok = 0;
    if (fd >= 0 && close(fd)) ok = 0;
    if (!ok || rename(TEMP, TARGET)) { unlink(TEMP); return 4; }
    printf("lilac GPU: Mesa library prepared in RAM (%llu bytes)\n", (unsigned long long)total);
    return 0;
}
