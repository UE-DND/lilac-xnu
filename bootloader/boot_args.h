/* SPDX-License-Identifier: BSD-2-Clause
 * Apple Boot Args Structure definition for XNU AArch64 (exact match to boot.h)
 */
#ifndef _BOOT_ARGS_H_
#define _BOOT_ARGS_H_

#include <stdint.h>

#define BOOT_LINE_LENGTH   608

struct Boot_Video {
    unsigned long   v_baseAddr;     /* Base address of video memory */
    unsigned long   v_display;      /* Display Code */
    unsigned long   v_rowBytes;     /* Number of bytes per pixel row */
    unsigned long   v_width;        /* Width (720) */
    unsigned long   v_height;       /* Height (1280) */
    unsigned long   v_depth;        /* Pixel Depth (32) */
};
typedef struct Boot_Video Boot_Video;

#define kBootArgsRevision2  2
#define kBootArgsVersion2   2

typedef struct boot_args {
    uint16_t        Revision;
    uint16_t        Version;
    uint64_t        virtBase;
    uint64_t        physBase;
    uint64_t        memSize;
    uint64_t        topOfKernelData;
    Boot_Video      Video;
    uint32_t        machineType;
    void            *deviceTreeP;
    uint32_t        deviceTreeLength;
    char            CommandLine[BOOT_LINE_LENGTH];
    uint64_t        bootFlags;
    uint64_t        memSizeActual;
} boot_args;

#endif /* _BOOT_ARGS_H_ */
