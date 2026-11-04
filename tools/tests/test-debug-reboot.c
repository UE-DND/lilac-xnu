#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../bootloader/debug-reboot.h"

static uint8_t pmic[256];
static uint32_t version, owner, restart_reason, pmic_status, read_data, write_data;
static unsigned operations, fail_at, writes, reset_writes, quiet_time, unsupported_map;

static int test_read(uint64_t address, uint32_t *value)
{
    if (++operations == fail_at) return 0;
    if (address == 0x0800f000) *value = version;
    else if (address == 0x0800f81c) *value = unsupported_map ? 0 : 8 << 8;
    else if (address >= 0x0800f800 && address < 0x08010000) *value = 0;
    else if (address == 0x0800a71c) *value = owner;
    else if (address == LILAC_RESTART_REASON) *value = restart_reason;
    else if (address == LILAC_PS_HOLD) *value = 1;
    else if (address == 0x09438008 || address == 0x08438008) *value = pmic_status;
    else if (address == 0x09438018) *value = read_data;
    else { assert(0); return 0; }
    return 1;
}

static int test_write(uint64_t address, uint32_t value)
{
    if (++operations == fail_at) return 0;
    if (address == LILAC_RESTART_REASON) restart_reason = value;
    else if (address == LILAC_PS_HOLD) reset_writes++;
    else if (address == 0x08438010) write_data = value;
    else if (address == 0x09438000) {
        assert((value >> 27) == 1 && !(value & 15));
        read_data = pmic[(value >> 4) & 255];
    } else if (address == 0x08438000) {
        assert(!(value >> 27) && !(value & 15));
        unsigned offset = (value >> 4) & 255;
        assert(offset == 0x5a || offset == 0x5b || offset == 0x8f);
        if (offset == 0x5a) { assert(!(pmic[0x5b] & 0x80)); assert(quiet_time >= 500); }
        if (offset == 0x5b && !(write_data & 0x80)) quiet_time = 0;
        pmic[offset] = write_data;
        writes++;
    } else { assert(0); return 0; }
    return 1;
}

static void test_delay(unsigned microseconds) { quiet_time += microseconds; }
static const struct lilac_reboot_io io = {test_read, test_write, test_delay};

static void reset_test(void)
{
    memset(pmic, 0, sizeof(pmic));
    pmic[5] = 4; pmic[0x5a] = 0xa7; pmic[0x5b] = 0x81; pmic[0x8f] = 0xab;
    version = 0x30000000; restart_reason = 0xc0dedead; pmic_status = 1;
    owner = operations = fail_at = writes = reset_writes = quiet_time = unsupported_map = 0;
}

int main(void)
{
    reset_test(); assert(lilac_prepare_fastboot(&io));
    assert(pmic[0x5a] == 0xa1 && pmic[0x5b] == 0x81 && pmic[0x8f] == 7);
    assert(restart_reason == 0x77665500 && writes == 4 && !reset_writes);
    unsigned successful_operations = operations;
    for (unsigned failure = 1; failure <= successful_operations; failure++) {
        reset_test(); fail_at = failure;
        assert(!lilac_prepare_fastboot(&io));
        assert(pmic[0x5a] == 0xa7 && pmic[0x5b] == 0x81 && pmic[0x8f] == 0xab);
        assert(restart_reason == 0xc0dedead && !reset_writes);
    }
    reset_test(); owner = 1; assert(!lilac_prepare_fastboot(&io) && !writes);
    reset_test(); version = 0x50000000; assert(!lilac_prepare_fastboot(&io) && !writes);
    reset_test(); version = 0x10000000; assert(!lilac_prepare_fastboot(&io) && !writes);
    reset_test(); unsupported_map = 1; assert(!lilac_prepare_fastboot(&io) && !writes);
    reset_test(); pmic[5] = 1; assert(!lilac_prepare_fastboot(&io) && !writes);
    reset_test(); pmic_status = 5; assert(!lilac_prepare_fastboot(&io) && !writes);
    reset_test(); pmic_status = 0; assert(!lilac_prepare_fastboot(&io) && !writes && quiet_time == 100);
    uint8_t value = 0;
    assert(!lilac_pon_byte(&io, 7, 0x42, &value, 1));
    assert(!lilac_pon_byte(&io, 7, 5, &value, 1));
    assert(!lilac_pon_byte(&io, 511, 0x8f, &value, 1));
    puts("PASS: Sony Gen2 PON warm reset/reason, permission checks, bounded transactions and failure rollback");
}
