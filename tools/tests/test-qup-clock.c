#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-qup-clock.h"

struct fake_clock {
    uint32_t vote, ahb, core, command, config;
    unsigned config_writes, command_writes, core_writes, delayed;
    int update_timeout, core_timeout, bad_readback;
};

static uint32_t clock_read(void *context, uint32_t offset)
{
    struct fake_clock *fake = context;
    switch (offset) {
    case LILAC_QUP_CLOCK_VOTE: return fake->vote;
    case LILAC_QUP_CLOCK_AHB: return fake->ahb;
    case LILAC_QUP_CLOCK_CORE: return fake->core;
    case LILAC_QUP_CLOCK_COMMAND: return fake->command;
    case LILAC_QUP_CLOCK_CONFIG: return fake->config;
    default: assert(0); return 0;
    }
}

static void clock_write(void *context, uint32_t offset, uint32_t value)
{
    struct fake_clock *fake = context;
    switch (offset) {
    case LILAC_QUP_CLOCK_VOTE: fake->vote = value; break;
    case LILAC_QUP_CLOCK_CORE:
        fake->core_writes++;
        fake->core = fake->core_timeout ? value : value & ~(1u << 31);
        break;
    case LILAC_QUP_CLOCK_COMMAND:
        fake->command_writes++;
        fake->command = fake->update_timeout ? value : value & ~1u;
        break;
    case LILAC_QUP_CLOCK_CONFIG:
        fake->config_writes++;
        fake->config = fake->bad_readback ? 0x101 : value;
        break;
    default: assert(0);
    }
}

static void clock_delay(void *context, uint32_t microseconds)
{
    struct fake_clock *fake = context;
    assert(microseconds == 10);
    fake->delayed += microseconds;
    assert(fake->delayed <= 10000);
}

int main(void)
{
    assert(LILAC_QUP_CLOCK_AHB == 0x17004 && LILAC_QUP_CLOCK_AHB_VOTE == (1u << 17));
    assert(LILAC_QUP_CLOCK_CORE == 0x21008 && LILAC_QUP_CLOCK_RESET == 0x21000);
    assert(LILAC_QUP_CLOCK_COMMAND == 0x21020 && LILAC_QUP_CLOCK_CONFIG == 0x21024);
    struct fake_clock fake;
    const struct lilac_qup_clock clock = {
        .context = &fake, .read = clock_read, .write = clock_write, .delay_us = clock_delay
    };
    for (uint32_t config = 0; config < 0x4000; config++) {
        int supported = !(config & 0x3700) && (config & 0x1f) <= 1;
        assert(lilac_qup_clock_is_xo(config) == supported);
        fake = (struct fake_clock){.ahb = 0x2000c220, .core = 1, .config = config, .vote = 0x25};
        assert(lilac_qup_clock_enable(&clock) ==
            (supported ? LILAC_QUP_CLOCK_OK : LILAC_QUP_CLOCK_UNSUPPORTED));
        assert(fake.vote == (0x25 | (1u << 17)));
        assert(!fake.config_writes && !fake.command_writes && !fake.core_writes && !fake.delayed);
    }
    fake = (struct fake_clock){.core = 0x80000000, .config = 0x10010b, .command = 0x10};
    assert(lilac_qup_clock_enable(&clock) == LILAC_QUP_CLOCK_OK);
    assert(fake.config == 0x100001 && fake.command == 0x10 && fake.core == 1);
    assert(fake.config_writes == 1 && fake.command_writes == 1 && fake.core_writes == 1);

    for (unsigned fault = 0; fault < 6; fault++) {
        fake = (struct fake_clock){.core = 0x80000000};
        enum lilac_qup_clock_error expected = LILAC_QUP_CLOCK_OK;
        switch (fault) {
        case 0: fake.ahb = 1u << 31; expected = LILAC_QUP_CLOCK_AHB_TIMEOUT; break;
        case 1: fake.command = 1; expected = LILAC_QUP_CLOCK_UPDATE_TIMEOUT; break;
        case 2: fake.update_timeout = 1; expected = LILAC_QUP_CLOCK_UPDATE_TIMEOUT; break;
        case 3: fake.core_timeout = 1; expected = LILAC_QUP_CLOCK_CORE_TIMEOUT; break;
        case 4: fake.bad_readback = 1; expected = LILAC_QUP_CLOCK_READBACK; break;
        case 5: fake.core = 0x80000001; expected = LILAC_QUP_CLOCK_CORE_TIMEOUT; break;
        }
        assert(lilac_qup_clock_enable(&clock) == expected);
        assert(fake.delayed == (fault == 4 ? 0 : 10000));
        if (fault < 2 || fault == 5) assert(!fake.config_writes && !fake.command_writes && !fake.core_writes);
        if (fault == 2 || fault == 4) assert(!fake.core_writes);
        assert(strcmp(lilac_qup_clock_error_name(expected), "ok"));
    }
    puts("PASS: inherited XO bypass/divide-by-one, invalid rates, bounded clock failures and readback");
}
