#ifndef LILAC_QUP_CLOCK_H
#define LILAC_QUP_CLOCK_H
#include <stdint.h>

enum lilac_qup_clock_error {
    LILAC_QUP_CLOCK_OK,
    LILAC_QUP_CLOCK_AHB_TIMEOUT,
    LILAC_QUP_CLOCK_UNSUPPORTED,
    LILAC_QUP_CLOCK_UPDATE_TIMEOUT,
    LILAC_QUP_CLOCK_CORE_TIMEOUT,
    LILAC_QUP_CLOCK_READBACK
};

enum {
    LILAC_QUP_CLOCK_VOTE = 0x52004,
    LILAC_QUP_CLOCK_AHB = 0x17004,
    LILAC_QUP_CLOCK_CORE = 0x21008,
    LILAC_QUP_CLOCK_COMMAND = 0x21020,
    LILAC_QUP_CLOCK_CONFIG = 0x21024,
    LILAC_QUP_CLOCK_RESET = 0x21000,
    LILAC_QUP_CLOCK_AHB_VOTE = 1u << 17
};

struct lilac_qup_clock {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    void (*delay_us)(void *, uint32_t);
};

static inline int lilac_qup_clock_is_xo(uint32_t config)
{
    return (config & 0x371eu) == 0;
}

static inline int lilac_qup_clock_wait(const struct lilac_qup_clock *clock,
    uint32_t offset, uint32_t mask)
{
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        if (!(clock->read(clock->context, offset) & mask)) return 1;
        clock->delay_us(clock->context, 10);
    }
    return 0;
}

static inline enum lilac_qup_clock_error
lilac_qup_clock_enable(const struct lilac_qup_clock *clock)
{
    uint32_t vote = clock->read(clock->context, LILAC_QUP_CLOCK_VOTE);
    clock->write(clock->context, LILAC_QUP_CLOCK_VOTE, vote | LILAC_QUP_CLOCK_AHB_VOTE);
    if (!lilac_qup_clock_wait(clock, LILAC_QUP_CLOCK_AHB, 1u << 31))
        return LILAC_QUP_CLOCK_AHB_TIMEOUT;
    if (!lilac_qup_clock_wait(clock, LILAC_QUP_CLOCK_COMMAND, 1))
        return LILAC_QUP_CLOCK_UPDATE_TIMEOUT;
    uint32_t branch = clock->read(clock->context, LILAC_QUP_CLOCK_CORE);
    uint32_t config = clock->read(clock->context, LILAC_QUP_CLOCK_CONFIG);
    if (branch & 1) {
        if (!lilac_qup_clock_is_xo(config)) return LILAC_QUP_CLOCK_UNSUPPORTED;
    } else {
        clock->write(clock->context, LILAC_QUP_CLOCK_CONFIG, (config & ~0x371fu) | 1);
        uint32_t command = clock->read(clock->context, LILAC_QUP_CLOCK_COMMAND);
        clock->write(clock->context, LILAC_QUP_CLOCK_COMMAND, command | 1);
        if (!lilac_qup_clock_wait(clock, LILAC_QUP_CLOCK_COMMAND, 1))
            return LILAC_QUP_CLOCK_UPDATE_TIMEOUT;
        if (!lilac_qup_clock_is_xo(clock->read(clock->context, LILAC_QUP_CLOCK_CONFIG)))
            return LILAC_QUP_CLOCK_READBACK;
        clock->write(clock->context, LILAC_QUP_CLOCK_CORE, branch | 1);
    }
    if (!lilac_qup_clock_wait(clock, LILAC_QUP_CLOCK_CORE, 1u << 31))
        return LILAC_QUP_CLOCK_CORE_TIMEOUT;
    return LILAC_QUP_CLOCK_OK;
}

static inline const char *lilac_qup_clock_error_name(uint32_t error)
{
    switch (error) {
    case LILAC_QUP_CLOCK_OK: return "ok";
    case LILAC_QUP_CLOCK_AHB_TIMEOUT: return "AHB clock timeout";
    case LILAC_QUP_CLOCK_UNSUPPORTED: return "unsupported inherited clock";
    case LILAC_QUP_CLOCK_UPDATE_TIMEOUT: return "RCG update timeout";
    case LILAC_QUP_CLOCK_CORE_TIMEOUT: return "core clock timeout";
    case LILAC_QUP_CLOCK_READBACK: return "clock config readback failed";
    default: return "unknown clock error";
    }
}
#endif
