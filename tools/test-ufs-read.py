#!/usr/bin/env python3
"""Exercise the production read-only UFS batch using a bounded fake controller."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware-pages', type=Path, help='8KiB retired slot fixture; capture or explicitly labelled photo reconstruction')
args = parser.parse_args()
firmware = args.firmware_pages.read_bytes() if args.firmware_pages else None
if firmware is not None and len(firmware) != 8192:
    parser.error('--firmware-pages must contain exactly two 4KiB pages')
base = struct.unpack_from('<I', firmware, 16)[0] - 0x100 if firmware else 0x9ab53000
if base % 4096 or not 0x99000000 <= base <= 0x9b000000 - 8192:
    parser.error('firmware slot0 must point to its bounded page+0x100 UCD')

program = r'''#define LILAC_UFS_READ_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ufs-read.c"
#define BASE BASE_VALUE
static uint32_t mem[2048], original[2048], regs[0x84/4];
static uint32_t calls, writes, ram_writes, ticks, scenario;
static uint8_t main_header[4096], backup_header[4096], table[128*1024];
static uint32_t table_bytes, table_blocks, partition_count, expected_calls;
FIRMWARE_ARRAY
/* The simulated disk is independent of the command builder and wire whitelist. */
enum { NORMAL, BAD_LUN, BAD_TM, BUSY, BAD_POINTER, BAD_LAYOUT, UNSUPPORTED,
       TIMEOUT, STALE_DMA, BAD_TAG, BAD_OCS, BAD_CRC, UNSTABLE, BAD_BACKUP,
       STOP_TIMEOUT, START_FAILED, CORRUPT_PRDT, WRONG_STATE, FATAL_STATUS,
       SHORT_RESPONSE, MOVED_QUEUE, FAILED_RESTORE, NO_COUNTER, CT_UFS, FIRMWARE,
       OLD_WRITE, OLD_MISMATCH, OLD_BUSY_OCS, OLD_SHORT_PRDT, OLD_BAD_CDB,
       BAD_ENTRIES_CRC, BAD_BACKUP_ARRAY, BAD_REPEAT_ARRAY, BAD_PARTITION_RANGE,
       OVERLAP, DUP_GUID, PARTITION_UNSTABLE, FINAL_UNSTABLE, MAX_BATCH,
       OLD_PARTIAL_RESPONSE, CHANGED_LAYOUT, OLD_BAD_DMA, TOO_MANY_ENTRIES,
       NO_PARTITIONS, ODD_ENTRIES, STUCK_COUNTER, LATE_TIMEOUT, LATE_MOVED_QUEUE };
static void putle32(uint8_t *p,uint32_t x) { for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(x>>(8*i)); }
static void putle64(uint8_t *p,uint64_t x) { putle32(p,(uint32_t)x);putle32(p+4,(uint32_t)(x>>32)); }
static void header(uint8_t *p,uint32_t own,uint32_t other,uint32_t first_entries,unsigned count,unsigned stride) {
    memset(p,0,4096);memcpy(p,"EFI PART",8);putle32(p+8,0x10000);putle32(p+12,92);
    putle64(p+24,own);putle64(p+32,other);putle64(p+40,scenario==MAX_BATCH?34:6);putle64(p+48,9999-table_blocks-1);
    memset(p+56,0x42,16);putle64(p+72,first_entries);putle32(p+80,count);putle32(p+84,stride);
    putle32(p+88,crc32(table,table_bytes,0));putle32(p+16,crc32(p,92,1));
}
static void disk_setup(void) {
    unsigned count=scenario==ODD_ENTRIES?127:128, stride=scenario==MAX_BATCH?1024:128;
    partition_count=scenario==MAX_BATCH?128:3;table_bytes=count*stride;table_blocks=(table_bytes+4095)/4096;
    expected_calls=8+3*table_blocks+3*partition_count;
    memset(table,0,sizeof(table));
    for (unsigned i=0;i<partition_count;i++) {
        uint8_t *p=table+i*stride;memset(p,0x11,16);putle32(p+16,i+1);
        uint64_t first=i==0?16:i==1?32:9001, last=i==0?31:i==1?9000:9994;
        if (scenario==MAX_BATCH) first=last=100+i*2;
        putle64(p+32,first);putle64(p+40,last);
        const char *name=i==0?"boot":i==1?"system":"misc";
        for (unsigned k=0;name[k];k++) p[56+k*2]=(uint8_t)name[k];
    }
    if (scenario==BAD_PARTITION_RANGE) putle64(table+32,5);
    if (scenario==OVERLAP) putle64(table+128+32,30);
    if (scenario==DUP_GUID) memcpy(table+128+16,table+16,16);
    if (scenario==NO_PARTITIONS) memset(table,0,table_bytes);
    header(main_header,1,9999,2,count,stride);
    header(backup_header,9999,1,9999-table_blocks,count,stride);
    if (scenario==TOO_MANY_ENTRIES) {
        putle32(main_header+80,129);putle32(main_header+16,crc32(main_header,92,1));
    }
}
uint32_t ur_reg(uint32_t offset) {
    assert(offset%4==0 && offset<=0x80 && !(offset>=0x38 && offset<=0x48));
    return regs[offset/4];
}
void ur_set_reg(uint32_t offset,uint32_t value) {
    assert((offset==TRRUN && value<=1) || (offset==TRDB && value==1));writes++;
    if (offset==TRRUN) {
        assert(regs[TRDB/4]==0);
        if (scenario==STOP_TIMEOUT && value==0) return;
        if (scenario==START_FAILED && value==1 && calls==0) return;
        regs[offset/4]=value;return;
    }
    assert(regs[TRRUN/4]==1 && regs[TRDB/4]==0 && regs[TMRUN/4]==0 && regs[TMDB/4]==0);
    const uint8_t *u=(uint8_t *)mem+UCD;uint8_t op=u[16],tag=u[3];
    uint32_t length=be32(u+12),lba=be32(u+18);
    assert(u[0]==1 && u[1]==0x40 && u[2]==0);
    assert(mem[0]==0x05000000 || mem[0]==0x15000000);
    assert(mem[4]==BASE+UCD && mem[5]==0 && mem[6]==0x00080010 && mem[7]==0x00180001);
    assert(mem[PRDT/4]==BASE+4096 && mem[PRDT/4+1]==0 && mem[PRDT/4+2]==0 && mem[PRDT/4+3]==length-1);
    assert((op==0x25 && length==8) || (op==0x28 && length==4096 && lba<=9999));
    assert(calls<512);calls++;regs[TRDB/4]=1;
    if (scenario==TIMEOUT || scenario==STUCK_COUNTER || (scenario==LATE_TIMEOUT && calls==6)) return;
    regs[TRDB/4]=0;if (scenario==STALE_DMA) return;
    uint8_t *dst=(uint8_t *)mem+4096,*reply=(uint8_t *)mem+RESPONSE;memset(dst,0,4096);
    if (op==0x25) { putbe32(dst,9999);putbe32(dst+4,scenario==UNSUPPORTED?512:4096); }
    else if (lba==0) {
        dst[510]=0x55;dst[511]=0xaa;
        if (scenario==FINAL_UNSTABLE && calls>5) dst[400]=1;
    } else if (lba==1 || lba==9999) {
        memcpy(dst,lba==1?main_header:backup_header,4096);
        if (scenario==BAD_CRC || (scenario==BAD_BACKUP && lba==9999)) dst[40]^=1;
        if (scenario==UNSTABLE && calls==4) dst[1000]^=1;
    } else if ((lba>=2 && lba<2+table_blocks) || (lba>=9999-table_blocks && lba<9999)) {
        unsigned off=(lba>=9999-table_blocks?lba-(9999-table_blocks):lba-2)*4096;
        unsigned n=table_bytes-off<4096?table_bytes-off:4096;memcpy(dst,table+off,n);
        memset(dst+n,0xcc,4096-n); /* Tail padding excluded from array CRC/compare. */
        if (scenario==BAD_ENTRIES_CRC && calls==6) dst[2000]^=1;
        if (scenario==BAD_BACKUP_ARRAY && calls==6+table_blocks) dst[2000]^=1;
        if (scenario==BAD_REPEAT_ARRAY && calls==6+2*table_blocks) dst[2000]^=1;
    } else {
        /* Only validated partition boundaries are allowed, not arbitrary LBAs. */
        unsigned found=0;
        for (unsigned i=0;i<partition_count;i++) {
            const uint8_t *p=table+i*(scenario==MAX_BATCH?1024:128);
            if (lba==le64(p+32) || lba==le64(p+40)) found=1;
        }
        assert(found);putle32(dst,lba);
        if (lba==16) memcpy(dst,"ANDROID!",8);
        if (lba==32) {dst[1080]=0x53;dst[1081]=0xef;}
        if (scenario==PARTITION_UNSTABLE && calls==7+3*table_blocks) dst[1000]^=1;
    }
    mem[2]=scenario==BAD_OCS?1:0;reply[0]=0x21;reply[3]=scenario==BAD_TAG?tag+1:tag;
    if (scenario==SHORT_RESPONSE) reply[1]=0x20;
    if (scenario==FATAL_STATUS) regs[IS/4]|=1u<<16;
    if (scenario==MOVED_QUEUE || (scenario==LATE_MOVED_QUEUE && calls==6)) regs[TRBA/4]+=4096;
}
uint32_t ur_load(uint64_t address) {
    assert(address>=BASE && address<BASE+8192 && address%4==0);
    if (scenario==CHANGED_LAYOUT && ram_writes==0 && address==BASE+4000) mem[1000]^=1;
    return mem[(address-BASE)/4];
}
void ur_store(uint64_t address,uint32_t value) {
    assert(address>=BASE && address<BASE+8192 && address%4==0);
    assert(regs[TRDB/4]==0 && regs[TRRUN/4]==0 && regs[TMRUN/4]==0 && regs[TMDB/4]==0);ram_writes++;
    if (scenario==CORRUPT_PRDT && address==BASE+PRDT) value^=4;
    if (scenario==FAILED_RESTORE && calls==expected_calls && address==BASE+4096) value^=1;
    mem[(address-BASE)/4]=value;
}
uint64_t ur_ticks(void) { if (scenario!=STUCK_COUNTER) ticks+=10000;return ticks; }
uint64_t ur_frequency(void) { return scenario==NO_COUNTER?0:1000000; }
void ur_barrier(void) {}
void lilac_ufs_read_progress(uint32_t s,uint32_t detail) { (void)s;(void)detail; }
static void setup(uint32_t sc) {
    scenario=sc;calls=writes=ram_writes=ticks=0;memset(regs,0,sizeof(regs));memset(mem,0xaf,sizeof(mem));
    regs[CAP/4]=0x1587001f;regs[VER/4]=0x210;regs[HCS/4]=0x10f;regs[HCE/4]=1;
    regs[IS/4]=0x400;regs[TRBA/4]=BASE;regs[TMBA/4]=BASE+4096;regs[TRRUN/4]=1;
    mem[0]=sc==CT_UFS?0x15000000:0x05000000;
    mem[1]=mem[2]=mem[3]=mem[5]=0;mem[4]=BASE+UCD;mem[6]=0x00080010;mem[7]=0x00180001;
    uint8_t *u=(uint8_t *)mem+UCD,*res=(uint8_t *)mem+RESPONSE;
    memset(u,0,0x80);u[0]=1;u[1]=0x40;u[3]=0xb1;u[16]=0x28;u[21]=6;u[24]=2;putbe32(u+12,8192);
    res[0]=0x21;res[3]=0xb1;mem[PRDT/4]=0x98fd6018;mem[PRDT/4+3]=8191;
    if (sc==BAD_LUN) u[2]=0xc4;
    if (sc==BAD_TM) regs[TMRUN/4]=1;
    if (sc==BUSY) regs[TRDB/4]=1;
    if (sc==BAD_POINTER) regs[TRBA/4]=0x12340000;
    if (sc==BAD_LAYOUT) mem[4]+=4096;
    if (sc==WRONG_STATE) regs[IE/4]=1;
    if (sc==OLD_WRITE) {mem[0]=0x03000000;u[1]=0x20;u[16]=0x2a;u[17]=8;}
    if (sc==OLD_MISMATCH) {mem[0]=0x03000000;u[16]=0x2a;u[17]=8;}
    if (sc==OLD_BUSY_OCS) mem[2]=0xf;
    if (sc==OLD_SHORT_PRDT) mem[PRDT/4+3]--;
    if (sc==OLD_BAD_CDB) u[22]=1;
    if (sc==OLD_PARTIAL_RESPONSE) res[1]=0x20;
    if (sc==OLD_BAD_DMA) mem[PRDT/4]=0xfffffffc;
    FIRMWARE_SETUP
    memcpy(original,mem,sizeof(mem));disk_setup();
}
int main(void) {
    static const struct {uint32_t sc,error,count,restored;} cases[]={
        {NORMAL,UR_OK,29,1},{CT_UFS,UR_OK,29,1},{OLD_WRITE,UR_OK,29,1},
        {BAD_LUN,UR_BAD_LAYOUT,0,0},{BAD_TM,UR_BAD_STATE,0,0},{BUSY,UR_BAD_STATE,0,0},
        {BAD_POINTER,UR_BAD_STATE,0,0},{BAD_LAYOUT,UR_BAD_LAYOUT,0,0},{UNSUPPORTED,UR_BAD_CAPACITY,1,1},
        {TIMEOUT,UR_IO_TIMEOUT,1,0},{STUCK_COUNTER,UR_IO_TIMEOUT,1,0},{STALE_DMA,UR_RESPONSE_FAILED,1,1},
        {BAD_TAG,UR_RESPONSE_FAILED,1,1},{BAD_OCS,UR_RESPONSE_FAILED,1,1},{BAD_CRC,UR_BAD_GPT,3,1},
        {UNSTABLE,UR_UNSTABLE,4,1},{BAD_BACKUP,UR_BAD_GPT,5,1},{STOP_TIMEOUT,UR_RESTORE_FAILED,0,0},
        {START_FAILED,UR_RESTORE_FAILED,0,0},{CORRUPT_PRDT,UR_RESTORE_FAILED,0,0},{WRONG_STATE,UR_BAD_STATE,0,0},
        {FATAL_STATUS,UR_RESPONSE_FAILED,1,1},{SHORT_RESPONSE,UR_RESPONSE_FAILED,1,1},{MOVED_QUEUE,UR_RESPONSE_FAILED,1,0},
        {FAILED_RESTORE,UR_RESTORE_FAILED,29,0},{NO_COUNTER,UR_BAD_STATE,0,0},
        {OLD_MISMATCH,UR_BAD_LAYOUT,0,0},{OLD_BUSY_OCS,UR_BAD_LAYOUT,0,0},{OLD_SHORT_PRDT,UR_BAD_LAYOUT,0,0},
        {OLD_BAD_CDB,UR_BAD_LAYOUT,0,0},{OLD_PARTIAL_RESPONSE,UR_BAD_LAYOUT,0,0},{OLD_BAD_DMA,UR_BAD_LAYOUT,0,0},
        {CHANGED_LAYOUT,UR_BAD_LAYOUT,0,0},{BAD_ENTRIES_CRC,UR_BAD_ENTRIES,9,1},
        {BAD_BACKUP_ARRAY,UR_UNSTABLE,10,1},{BAD_REPEAT_ARRAY,UR_UNSTABLE,14,1},
        {BAD_PARTITION_RANGE,UR_BAD_PARTITION,17,1},{OVERLAP,UR_BAD_PARTITION,17,1},{DUP_GUID,UR_BAD_PARTITION,17,1},
        {PARTITION_UNSTABLE,UR_UNSTABLE,19,1},{FINAL_UNSTABLE,UR_UNSTABLE,28,1},
        {MAX_BATCH,UR_OK,488,1},{TOO_MANY_ENTRIES,UR_BAD_GPT,3,1},{NO_PARTITIONS,UR_BAD_PARTITION,17,1},
        {ODD_ENTRIES,UR_OK,29,1},{LATE_TIMEOUT,UR_IO_TIMEOUT,6,0},{LATE_MOVED_QUEUE,UR_RESPONSE_FAILED,6,0},
        FIRMWARE_CASE
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        setup(cases[i].sc);static struct ur_result r;lilac_ufs_read_probe(&r);
        if (r.error!=cases[i].error || calls!=cases[i].count || r.restored!=cases[i].restored) {
            fprintf(stderr,"scenario %u error %u calls %u restored %u\n",scenario,r.error,calls,r.restored);return 1;
        }
        assert(r.submitted==calls);
        if (r.restored) assert(!memcmp(mem,original,sizeof(mem)) && regs[TRRUN/4]==1);
        if (r.error==UR_BAD_STATE || r.error==UR_BAD_LAYOUT) assert(writes==0 && ram_writes==0);
        if (r.error==UR_IO_TIMEOUT) assert(!r.restored && regs[TRDB/4]==1);
        if (r.error==UR_OK) {
            assert(r.checks==63 && r.partitions==partition_count && r.sampled==partition_count && r.stage==UR_DONE);
            assert(r.primary_entries_crc==r.entries_crc && r.backup_entries_crc==r.entries_crc && r.repeat_entries_crc==r.entries_crc);
            if (scenario!=MAX_BATCH) assert(!strcmp(r.partition[0].name,"boot") && r.partition[0].signature==1 && r.partition[1].signature==3);
        }
    }
    /* Fast boot still verifies both GPTs and all partition bounds, but does
       not claim or perform partition payload samples. */
    static struct ur_result fast;
    setup(NORMAL);lilac_ufs_read_probe_fast(&fast);
    assert(!fast.error && fast.restored && fast.checks==47 && fast.partitions==partition_count &&
           fast.sampled==0 && calls==20 && !memcmp(mem,original,sizeof(mem)));
    setup(BAD_BACKUP_ARRAY);lilac_ufs_read_probe_fast(&fast);
    assert(fast.error==UR_UNSTABLE && fast.restored && calls==10);
    setup(OVERLAP);lilac_ufs_read_probe_fast(&fast);
    assert(fast.error==UR_BAD_PARTITION && fast.restored && calls==17);
    /* Wire mutation tests apply at every submission. Completed old WRITE
       acceptance cannot turn WRITE, UNMAP or Query into a generated command. */
    uint8_t u[32]={1,0x40,0,0xc0};putbe32(u+12,4096);u[16]=0x28;u[24]=1;
    assert(permitted(u,0x05000000,4096,9999,4096));unsigned rejected=0;
    for (unsigned i=0;i<32;i++) for (unsigned bit=0;bit<8;bit++) {
        if (i==3 || (i>=18 && i<=21)) continue;
        u[i]^=1u<<bit;assert(!permitted(u,0x05000000,4096,9999,4096));u[i]^=1u<<bit;rejected++;
    }
    assert(!permitted(u,0x03000000,4096,9999,4096));u[16]=0x2a;assert(!permitted(u,0x05000000,4096,9999,4096));u[16]=0x28;
    putbe32(u+18,10000);assert(!permitted(u,0x05000000,4096,9999,4096));
    /* Independently mutate every constrained retired-descriptor field. Its
       arbitrary tag, LBA and old data address are intentionally not templates. */
    setup(NORMAL);unsigned retired_rejected=0;
    for (unsigned word=0;word<8;word++) for (unsigned bit=0;bit<32;bit++) {
        if (word==0 && bit==28) continue; /* Both observed command-type encodings. */
        mem[word]^=1u<<bit;assert(!retired_layout(mem,BASE));mem[word]^=1u<<bit;retired_rejected++;
    }
    uint8_t *old=(uint8_t *)mem+UCD,*response=(uint8_t *)mem+RESPONSE;
    for (unsigned i=0;i<32;i++) for (unsigned bit=0;bit<8;bit++) {
        if (i==3 || (i>=18 && i<=21)) continue;
        old[i]^=1u<<bit;assert(!retired_layout(mem,BASE));old[i]^=1u<<bit;retired_rejected++;
    }
    for (unsigned word=1;word<4;word++) for (unsigned bit=0;bit<32;bit++) {
        mem[PRDT/4+word]^=1u<<bit;assert(!retired_layout(mem,BASE));mem[PRDT/4+word]^=1u<<bit;retired_rejected++;
    }
    static const unsigned constrained_response[]={0,1,2,3,6,7,10,11,12,13,14,15};
    for (unsigned i=0;i<sizeof(constrained_response)/sizeof(constrained_response[0]);i++) for (unsigned bit=0;bit<8;bit++) {
        unsigned index=constrained_response[i];response[index]^=1u<<bit;
        assert(!retired_layout(mem,BASE));response[index]^=1u<<bit;retired_rejected++;
    }
    /* The kernel adapter must restore a fresh snapshot after every bounded
       call, including on a completed response failure, and retain live DMA. */
    static struct ur_result range_result;
    static uint8_t range_data[3*4096+1];
    setup(NORMAL);memset(range_data,0x5a,sizeof(range_data));
    lilac_ufs_read_range(&range_result,9999,0,2,range_data);
    assert(!range_result.error && range_result.completed==2 && range_result.restored && calls==2);
    assert(range_data[510]==0x55 && !memcmp(range_data+4096,main_header,4096) && range_data[8192]==0x5a);
    assert(!memcmp(mem,original,sizeof(mem)));
    lilac_ufs_read_range(&range_result,9999,9999,1,range_data);
    assert(!range_result.error && range_result.completed==1 && range_result.restored && calls==3);
    assert(!memcmp(range_data,backup_header,4096) && !memcmp(mem,original,sizeof(mem)));
    static const uint32_t invalid[][3]={{9999,10000,1},{9999,9999,2},{9999,0,0},{9999,0,65},{0xffffffffu,0,1}};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        setup(NORMAL);lilac_ufs_read_range(&range_result,invalid[i][0],invalid[i][1],invalid[i][2],range_data);
        assert(range_result.error==UR_BAD_CAPACITY && !calls && !writes && !ram_writes);
    }
    setup(NORMAL);lilac_ufs_read_range(&range_result,9999,0,1,NULL);
    assert(range_result.error==UR_BAD_CAPACITY && !calls && !writes);
    setup(BAD_OCS);memset(range_data,0x5a,sizeof(range_data));
    lilac_ufs_read_range(&range_result,9999,0,1,range_data);
    assert(range_result.error==UR_RESPONSE_FAILED && range_result.restored && range_data[0]==0x5a && !memcmp(mem,original,sizeof(mem)));
    setup(TIMEOUT);lilac_ufs_read_range(&range_result,9999,0,1,range_data);
    assert(range_result.error==UR_IO_TIMEOUT && !range_result.restored && regs[TRDB/4]==1);
    setup(NORMAL);static struct ur_result limit;result=&limit;limit.submitted=UR_MAX_COMMANDS;
    assert(!transfer(0x25,0,8) && limit.error==UR_LIMIT && calls==0 && writes==0 && ram_writes==0);
    assert(crc32((const uint8_t *)"123456789",9,0)==0xcbf43926);
    printf("PASS: %zu batch/controller scenarios; %u READ wire and %u retired-layout mutations; kernel range restore/bounds/failure; maximum 488-command batch; CRC/restore/timeout/limit invariants\n",
           sizeof(cases)/sizeof(cases[0]),rejected,retired_rejected);
}
'''
program = program.replace('BASE_VALUE', hex(base) + 'u')
program = program.replace('FIRMWARE_ARRAY', 'static const uint8_t firmware[]={' + ','.join(map(str, firmware)) + '};' if firmware else '')
program = program.replace('FIRMWARE_SETUP', 'if (sc==FIRMWARE) memcpy(mem,firmware,8192);' if firmware else '')
program = program.replace('FIRMWARE_CASE', '{FIRMWARE,UR_OK,29,1},' if firmware else '')
with tempfile.TemporaryDirectory(prefix='ufs-read-') as tmp:
    source, binary = Path(tmp) / 'test.c', Path(tmp) / 'test'
    source.write_text(program)
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'bootloader'),
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
