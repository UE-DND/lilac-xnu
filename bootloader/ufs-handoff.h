/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LILAC_UFS_HANDOFF_H
#define LILAC_UFS_HANDOFF_H
#include <stdint.h>

enum uh_register { UH_CAP, UH_VER, UH_HCS, UH_HCE, UH_IS, UH_IE,
    UH_TRBA, UH_TRBAU, UH_TMBA, UH_TMBAU, UH_TRDB, UH_TRRUN,
    UH_TMDB, UH_TMRUN, UH_REGISTER_COUNT };
enum uh_error { UH_OK, UH_BAD_STATE, UH_CHANGED };
struct uh_result {
    uint32_t error, captured, stable;
    uint32_t reg[UH_REGISTER_COUNT];
    uint32_t descriptor[8], request[8], response[8], prdt[4];
    uint32_t tr_crc, tm_crc;
    uint32_t direction, opcode, lun, tag, lba, blocks, bytes, scsi_status;
    uint32_t read_guard;
};
void lilac_ufs_handoff_snapshot(struct uh_result *result);
void lilac_ufs_handoff_progress(uint32_t stage, uint32_t address);
#endif
