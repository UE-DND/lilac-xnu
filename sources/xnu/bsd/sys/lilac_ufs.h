/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Native read-only UFS 2.1 + GPT block/character storage driver for Sony
 * Xperia XZ1 Compact (MSM8998 / lilac, UFSHCI at 0x01da4000).
 */
#ifndef _SYS_LILAC_UFS_H_
#define _SYS_LILAC_UFS_H_

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

void lilac_ufs_init(void);
void lilac_ufs_report(const char *when);
bool lilac_ufs_dev(dev_t dev);
void lilac_ufs_quiesce(void);
int  lilac_ufs_read_partition(const char *name, uint64_t offset, uint8_t *dst,
         uint64_t len, uint64_t *bytes);

#endif /* _SYS_LILAC_UFS_H_ */
