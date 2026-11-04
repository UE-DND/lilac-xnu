#!/usr/bin/env python3
"""Check the production handoff observer's bounds and unchanged READ guard."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware-pages', type=Path, help='optional prior Fastboot 8KiB capture')
args = parser.parse_args()
firmware = args.firmware_pages.read_bytes() if args.firmware_pages else None
if firmware is not None and len(firmware) != 8192:
    parser.error('--firmware-pages must contain exactly two 4KiB pages')
base = struct.unpack_from('<I', firmware, 16)[0] - 0x100 if firmware else 0x9ab53000
if base % 4096 or not 0x99000000 <= base <= 0x9b000000 - 8192:
    parser.error('firmware slot0 must point to its bounded page+0x100 UCD')

program = r'''
#define LILAC_UFS_HANDOFF_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ufs-handoff.c"
#define BASE BASE_VALUE
static uint32_t mem[2048], original[2048], regs[0x84/4], reads, scenario;
FIRMWARE_ARRAY
enum { NORMAL, OLD_WRITE, HIGH_TR, LOW_TR, MISALIGNED, DISCONTIGUOUS,
       UPPER_TR, UPPER_TM, BUSY_TR, BUSY_TM, STOPPED_TR, RUNNING_TM,
       ENABLED_IRQ, DISABLED_HCE, NO_DEVICE, NO_READY, BAD_VER,
       ZERO_CAP, ONES_CAP, FATAL_IS, CHANGED_TR, CHANGED_TM, CHANGED_REG,
       WRONG_OCS, WRONG_POINTER, WRONG_OFFSETS, WRONG_RESPONSE, CT_UFS, FIRMWARE };
uint32_t uh_load(uint64_t address) {
    assert(address%4==0);
    if (address>=0x01da4000u && address<=0x01da4080u) {
        uint32_t offset=(uint32_t)address-0x01da4000u;
        unsigned found=0;
        for (unsigned i=0;i<UH_REGISTER_COUNT;i++) if (offsets[i]==offset) found=1;
        assert(found); /* No read-clear, vendor, command or list-clear reads. */
        if (scenario==CHANGED_REG && reads>=4096 && offset==0x50) return BASE+4096;
        return regs[offset/4];
    }
    assert(address>=BASE && address<BASE+8192); /* Never follow old PRD pointer. */
    reads++;
    if (reads==2049 && scenario==CHANGED_TR) mem[900]^=1;
    if (reads==2049 && scenario==CHANGED_TM) mem[1900]^=1;
    return mem[(address-BASE)/4];
}
void uh_barrier(void) {}
void lilac_ufs_handoff_progress(uint32_t stage, uint32_t address) {
    assert(stage>=1 && stage<=7);
    if (stage==7) assert(address==0);
}
static void setup(uint32_t sc) {
    scenario=sc; reads=0;
    memset(regs,0,sizeof(regs)); memset(mem,0xaf,sizeof(mem));
    regs[0]=0x1587001f; regs[0x08/4]=0x210; regs[0x30/4]=0x10f; regs[0x34/4]=1;
    regs[0x20/4]=0x400; regs[0x50/4]=BASE; regs[0x70/4]=BASE+4096; regs[0x60/4]=1;
    mem[0]=0x05000000; mem[1]=mem[2]=mem[3]=mem[5]=0;
    mem[4]=BASE+0x100; mem[6]=0x00080010; mem[7]=0x00180001;
    uint8_t *u=(uint8_t *)mem+0x100, *v=(uint8_t *)mem+0x120;
    memset(u,0,0x80); u[0]=1; u[1]=0x40; u[3]=0xb1; u[16]=0x28; u[21]=6; u[24]=2;
    u[14]=0x20; v[0]=0x21; v[3]=0xb1;
    mem[0x160/4]=0x98fd6018; mem[0x160/4+3]=8191;
    switch (sc) {
    case OLD_WRITE: mem[0]=0x03000000; u[1]=0x20; u[16]=0x2a; break;
    case HIGH_TR: regs[0x50/4]=0x9b000000; break;
    case LOW_TR: regs[0x50/4]=0x98fff000; break;
    case MISALIGNED: regs[0x50/4]=BASE+4; break;
    case DISCONTIGUOUS: regs[0x70/4]=BASE+8192; break;
    case UPPER_TR: regs[0x54/4]=1; break;
    case UPPER_TM: regs[0x74/4]=1; break;
    case BUSY_TR: regs[0x58/4]=1; break;
    case BUSY_TM: regs[0x78/4]=1; break;
    case STOPPED_TR: regs[0x60/4]=0; break;
    case RUNNING_TM: regs[0x80/4]=1; break;
    case ENABLED_IRQ: regs[0x24/4]=1; break;
    case DISABLED_HCE: regs[0x34/4]=0; break;
    case NO_DEVICE: regs[0x30/4]&=~1u; break;
    case NO_READY: regs[0x30/4]&=~2u; break;
    case BAD_VER: regs[0x08/4]=0x300; break;
    case ZERO_CAP: regs[0]=0; break;
    case ONES_CAP: regs[0]=~0u; break;
    case FATAL_IS: regs[0x20/4]|=1u<<16; break;
    case WRONG_OCS: mem[2]=1; break;
    case WRONG_POINTER: mem[4]+=4096; break;
    case WRONG_OFFSETS: mem[6]^=1; break;
    case WRONG_RESPONSE: v[7]=2; break;
    case CT_UFS: mem[0]=0x15000000; break;
    FIRMWARE_SETUP
    }
    memcpy(original,mem,sizeof(mem));
}
int main(void) {
    static const struct { uint32_t scenario, error, mask; } cases[]={
        {NORMAL,UH_OK,0}, {OLD_WRITE,UH_OK,0x11}, {CT_UFS,UH_OK,0},
        {HIGH_TR,UH_BAD_STATE,0}, {LOW_TR,UH_BAD_STATE,0}, {MISALIGNED,UH_BAD_STATE,0},
        {DISCONTIGUOUS,UH_BAD_STATE,0}, {UPPER_TR,UH_BAD_STATE,0}, {UPPER_TM,UH_BAD_STATE,0},
        {BUSY_TR,UH_BAD_STATE,0}, {BUSY_TM,UH_BAD_STATE,0}, {STOPPED_TR,UH_BAD_STATE,0},
        {RUNNING_TM,UH_BAD_STATE,0}, {ENABLED_IRQ,UH_BAD_STATE,0}, {DISABLED_HCE,UH_BAD_STATE,0},
        {NO_DEVICE,UH_BAD_STATE,0}, {NO_READY,UH_BAD_STATE,0}, {BAD_VER,UH_BAD_STATE,0},
        {ZERO_CAP,UH_BAD_STATE,0}, {ONES_CAP,UH_BAD_STATE,0}, {FATAL_IS,UH_BAD_STATE,0},
        {CHANGED_TR,UH_CHANGED,0x40}, {CHANGED_TM,UH_CHANGED,0x40}, {CHANGED_REG,UH_CHANGED,0x40},
        {WRONG_OCS,UH_OK,2}, {WRONG_POINTER,UH_OK,4}, {WRONG_OFFSETS,UH_OK,8},
        {WRONG_RESPONSE,UH_OK,0x20},
        FIRMWARE_CASE
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        setup(cases[i].scenario); struct uh_result r; lilac_ufs_handoff_snapshot(&r);
        assert(r.error==cases[i].error && r.read_guard==cases[i].mask);
        if (r.error==UH_BAD_STATE) assert(reads==0 && !r.captured && !r.stable);
        else {
            assert(reads==4096 && r.captured && r.stable==(r.error==UH_OK));
            assert(!memcmp(r.descriptor,original,32));
            assert(!memcmp(r.request,(uint8_t *)original+0x100,32));
            assert(!memcmp(r.response,(uint8_t *)original+0x120,32));
            assert(!memcmp(r.prdt,(uint8_t *)original+0x160,16));
            assert(r.tr_crc==page_crc(original) && r.tm_crc==page_crc(original+1024));
        }
        if (scenario!=CHANGED_TR && scenario!=CHANGED_TM) assert(!memcmp(mem,original,sizeof(mem)));
        if (scenario==NORMAL || scenario==OLD_WRITE) {
            assert(r.lun==0 && r.tag==0xb1 && r.lba==6 && r.blocks==2 && r.bytes==8192 && r.scsi_status==0);
            assert(r.opcode==(scenario==NORMAL?0x28u:0x2au));
            assert(r.direction==(scenario==NORMAL?2u:1u));
        }
    }
    /* CRC known vector; the separate test-ufs-read.py regression also checks
     * that the synthetic post-ABL WRITE causes zero reader submissions. */
    assert(page_crc((const uint32_t[1024]){0})==0xc71c0011);
    printf("PASS: %zu handoff scenarios; bounded loads; raw capture/CRC; stable/changed detection; WRITE remains diagnostic only\n",
        sizeof(cases)/sizeof(cases[0]));
}
'''
program = program.replace('FIRMWARE_ARRAY', 'static const uint8_t firmware[]={' + ','.join(map(str, firmware)) + '};' if firmware else '')
program = program.replace('BASE_VALUE', hex(base) + 'u')
program = program.replace('FIRMWARE_SETUP', 'case FIRMWARE: memcpy(mem,firmware,8192); break;' if firmware else '')
program = program.replace('FIRMWARE_CASE', '{FIRMWARE,UH_OK,0},' if firmware else '')
with tempfile.TemporaryDirectory(prefix='ufs-handoff-') as tmp:
    source, binary = Path(tmp) / 'test.c', Path(tmp) / 'test'
    source.write_text(program)
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'bootloader'),
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
