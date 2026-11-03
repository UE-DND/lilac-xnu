/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LILAC_UFS_READ_H
#define LILAC_UFS_READ_H
#include <stdint.h>

enum ur_stage { UR_PREFLIGHT = 1, UR_LAYOUT, UR_CAPACITY, UR_MBR,
    UR_PRIMARY, UR_REPEAT, UR_BACKUP, UR_RESTORE, UR_DONE,
    UR_ENTRIES, UR_BACKUP_ENTRIES, UR_REPEAT_ENTRIES, UR_PARTITIONS, UR_FINAL };
enum ur_error { UR_OK, UR_BAD_STATE, UR_BAD_LAYOUT, UR_STOP_TIMEOUT,
    UR_START_FAILED, UR_VERIFY_FAILED, UR_IO_TIMEOUT, UR_RESPONSE_FAILED,
    UR_BAD_CAPACITY, UR_BAD_MBR, UR_BAD_GPT, UR_UNSTABLE, UR_RESTORE_FAILED,
    UR_BAD_ENTRIES, UR_BAD_PARTITION, UR_LIMIT };
#define UR_MAX_PARTITIONS 128u
#define UR_MAX_COMMANDS 512u
struct ur_partition {
    char name[37]; /* GPT's 36 UTF-16 code units, ASCII or '?' for display. */
    uint32_t head_crc, tail_crc, signature;
    uint64_t first, last;
};
struct ur_result {
    uint32_t stage, error, completed, restored;
    uint32_t cap, version, hcs, is_before, is_after, ocs, response;
    uint32_t tr_base, tm_base, old_dw0, old_cdb;
    uint32_t block_bytes, last_lba, mbr_crc, primary_crc, backup_crc;
    uint32_t header_crc, entries_crc, entries_count, entry_bytes;
    uint64_t first_usable, last_usable;
    uint32_t submitted, failed_lba, doorbell, tr_run;
    uint32_t primary_entries_crc, backup_entries_crc, repeat_entries_crc;
    uint32_t partitions, sampled, checks, elapsed_ms;
    uint32_t response_words[4], data_words[4];
    struct ur_partition partition[UR_MAX_PARTITIONS];
};
enum ur_checks { UR_CHECK_CAPACITY=1, UR_CHECK_MBR=2, UR_CHECK_HEADERS=4,
    UR_CHECK_ENTRIES=8, UR_CHECK_PARTITIONS=16, UR_CHECK_FINAL=32 };
void lilac_ufs_read_probe(struct ur_result *result);
/* Full GPT/overlap validation, without the optional partition data samples. */
void lilac_ufs_read_probe_fast(struct ur_result *result);
void lilac_ufs_read_range(struct ur_result *result, uint32_t last, uint32_t lba,
    uint32_t count, uint8_t *destination);
void lilac_ufs_read_progress(uint32_t stage, uint32_t detail);
#endif
