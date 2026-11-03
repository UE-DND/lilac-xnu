/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LILAC_UFS_API_H
#define LILAC_UFS_API_H
#include <stdint.h>
#define LILAC_UFS_API_VERSION 1u
#define LILAC_UFS_MAX_PARTS 128u
struct lilac_ufs_part_info {
    char name[40];
    uint64_t first, last;
    uint32_t head_crc, tail_crc;
};
struct lilac_ufs_info {
    uint32_t version, ready, parts, block_bytes;
    uint32_t last_lba, checks, commands, restored;
    uint32_t mbr_crc, header_crc, entries_crc, reserved;
    struct lilac_ufs_part_info part[LILAC_UFS_MAX_PARTS];
};
#endif
