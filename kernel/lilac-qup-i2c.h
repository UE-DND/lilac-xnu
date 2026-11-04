#ifndef LILAC_QUP_I2C_H
#define LILAC_QUP_I2C_H
#include <stddef.h>
#include <stdint.h>
#include "lilac-qup-i2c-tags.h"

enum lilac_qup_register {
    LQ_CONFIG = 0x000, LQ_STATE = 0x004, LQ_IO_MODE = 0x008,
    LQ_SW_RESET = 0x00c, LQ_OPERATIONAL = 0x018, LQ_ERROR = 0x01c,
    LQ_ERROR_ENABLE = 0x020, LQ_OPERATIONAL_MASK = 0x028,
    LQ_VERSION = 0x030, LQ_OUTPUT_COUNT = 0x100, LQ_OUTPUT_FIFO = 0x110,
    LQ_WRITE_COUNT = 0x150, LQ_INPUT_COUNT = 0x200, LQ_READ_COUNT = 0x208,
    LQ_INPUT_FIFO = 0x218, LQ_CLOCK = 0x400, LQ_I2C_STATUS = 0x404,
    LQ_I2C_CONFIG = 0x408
};

enum lilac_qup_error {
    LQ_OK, LQ_ARGUMENT, LQ_BUSY, LQ_CAPACITY, LQ_STATE_TIMEOUT,
    LQ_NOT_I2C, LQ_BUS_ERROR, LQ_FIFO_ERROR, LQ_TRANSFER_TIMEOUT,
    LQ_SHORT_READ, LQ_RESET_FAILED
};

struct lilac_qup {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    void (*delay_us)(void *, uint32_t);
    uint32_t clock;
    uint32_t state, operational, bus_error, fifo_error;
    unsigned error;
};

static inline int lilac_qup_poll(struct lilac_qup *controller,
    uint32_t offset, uint32_t mask, uint32_t value, unsigned attempts)
{
    for (unsigned attempt = 0; attempt < attempts; attempt++) {
        if ((controller->read(controller->context, offset) & mask) == value)
            return 1;
        controller->delay_us(controller->context, 10);
    }
    return 0;
}

static inline int lilac_qup_state(struct lilac_qup *controller, uint32_t state)
{
    if (!lilac_qup_poll(controller, LQ_STATE, 4, 4, 100)) return 0;
    controller->write(controller->context, LQ_STATE, state);
    return lilac_qup_poll(controller, LQ_STATE, 7, state | 4, 100);
}

static inline unsigned lilac_qup_capacity(uint32_t mode, int input)
{
    static const unsigned block_bytes[] = {16, 16, 32};
    unsigned block = (mode >> (input ? 5 : 0)) & 3;
    unsigned size = (mode >> (input ? 7 : 2)) & 7;
    return block < 3 ? block_bytes[block] * (2u << size) : 0;
}

static inline int lilac_qup_initialize(struct lilac_qup *controller)
{
    controller->error = LQ_BUSY;
    if (controller->read(controller->context, LQ_I2C_STATUS) & (1u << 8)) return 0;
    controller->write(controller->context, LQ_SW_RESET, 1);
    if (!lilac_qup_poll(controller, LQ_STATE, 7, 4, 100)) {
        controller->error = LQ_RESET_FAILED;
        return 0;
    }
    controller->error = LQ_OK;
    return 1;
}

static inline void lilac_qup_snapshot(struct lilac_qup *controller)
{
    controller->state = controller->read(controller->context, LQ_STATE);
    controller->operational = controller->read(controller->context, LQ_OPERATIONAL);
    controller->bus_error = controller->read(controller->context, LQ_I2C_STATUS);
    controller->fifo_error = controller->read(controller->context, LQ_ERROR);
}

static inline int lilac_qup_transfer(void *context, uint8_t address,
    const uint8_t *write_data, size_t write_bytes, uint8_t *read_data, size_t read_bytes)
{
    struct lilac_qup *controller = context;
    uint8_t tags[9], received[82];
    size_t transmit = 0, receive = 0;
    unsigned error = LQ_OK;
    if (!controller) return LQ_ARGUMENT;
    controller->error = LQ_ARGUMENT;
    if (!controller->read || !controller->write || !controller->delay_us ||
        !write_data || address != 0x2c || read_bytes > 80 ||
        (read_bytes && (!read_data || write_bytes != 1 || write_data[0] == 255 ||
                        read_bytes > 255u - write_data[0])) ||
        (!read_bytes && write_bytes != 2)) return LQ_ARGUMENT;
    if (read_bytes) {
        if (!lilac_qup_register_read_tags(address, write_data[0], read_bytes,
            tags, sizeof(tags), &transmit, &receive)) return LQ_ARGUMENT;
    } else {
        tags[0] = 0x81; tags[1] = (uint8_t)(address << 1);
        tags[2] = 0x83; tags[3] = 2;
        tags[4] = write_data[0]; tags[5] = write_data[1]; transmit = 6;
    }
    lilac_qup_snapshot(controller);
    if (controller->bus_error & (1u << 8)) {
        controller->error = LQ_BUSY;
        return LQ_BUSY;
    }
    uint32_t mode = controller->read(controller->context, LQ_IO_MODE);
    if (transmit > lilac_qup_capacity(mode, 0) ||
        receive > lilac_qup_capacity(mode, 1)) {
        controller->error = LQ_CAPACITY;
        return LQ_CAPACITY;
    }
    controller->write(controller->context, LQ_SW_RESET, 1);
    if (!lilac_qup_poll(controller, LQ_STATE, 7, 4, 100)) {
        error = LQ_STATE_TIMEOUT; goto finished;
    }
    controller->write(controller->context, LQ_CONFIG, 0x207 | (receive ? 0 : 0x80));
    controller->write(controller->context, LQ_I2C_CONFIG, 1);
    if (!lilac_qup_poll(controller, LQ_STATE, 0x14, 0x14, 100)) {
        error = LQ_NOT_I2C; goto finished;
    }
    controller->write(controller->context, LQ_ERROR_ENABLE, 0x7c);
    controller->write(controller->context, LQ_OPERATIONAL_MASK, 0x300);
    controller->write(controller->context, LQ_IO_MODE, 0xc000);
    controller->write(controller->context, LQ_OUTPUT_COUNT, 0);
    controller->write(controller->context, LQ_INPUT_COUNT, 0);
    controller->write(controller->context, LQ_WRITE_COUNT, (uint32_t)transmit);
    controller->write(controller->context, LQ_READ_COUNT, (uint32_t)receive);
    if (!lilac_qup_state(controller, 1)) {
        error = LQ_STATE_TIMEOUT; goto finished;
    }
    controller->write(controller->context, LQ_CLOCK, controller->clock);
    if (!lilac_qup_state(controller, 3)) {
        error = LQ_STATE_TIMEOUT; goto finished;
    }
    for (size_t offset = 0; offset < transmit; offset += 4) {
        uint32_t word = 0;
        for (size_t byte = 0; byte < 4 && offset + byte < transmit; byte++)
            word |= (uint32_t)tags[offset + byte] << (8 * byte);
        controller->write(controller->context, LQ_OUTPUT_FIFO, word);
    }
    if (!lilac_qup_state(controller, 1)) {
        error = LQ_STATE_TIMEOUT; goto finished;
    }
    error = LQ_TRANSFER_TIMEOUT;
    for (unsigned attempt = 0; attempt < 5000; attempt++) {
        lilac_qup_snapshot(controller);
        if (controller->bus_error & 0x038000fcu) { error = LQ_BUS_ERROR; break; }
        if (controller->fifo_error & 0x7c) { error = LQ_FIFO_ERROR; break; }
        uint32_t done = receive ? 0x800 : 0x400;
        if ((controller->operational & done) && !(controller->bus_error & 0x100)) {
            error = LQ_OK; break;
        }
        controller->delay_us(controller->context, 10);
    }
    if (error) goto finished;
    for (size_t offset = 0; offset < receive; offset += 4) {
        if (!(controller->read(controller->context, LQ_OPERATIONAL) & 0x20)) {
            error = LQ_SHORT_READ; goto finished;
        }
        uint32_t word = controller->read(controller->context, LQ_INPUT_FIFO);
        for (size_t byte = 0; byte < 4 && offset + byte < receive; byte++)
            received[offset + byte] = (uint8_t)(word >> (8 * byte));
    }
finished:
    lilac_qup_snapshot(controller);
    if (!lilac_qup_state(controller, 0)) {
        controller->write(controller->context, LQ_SW_RESET, 1);
        if (!lilac_qup_poll(controller, LQ_STATE, 7, 4, 100)) error = LQ_RESET_FAILED;
    }
    controller->error = error;
    if (!error) {
        for (size_t offset = 0; offset < read_bytes; offset++)
            read_data[offset] = received[offset + 2];
    }
    return (int)error;
}
#endif
