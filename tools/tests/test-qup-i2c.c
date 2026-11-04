#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/lilac-qup-i2c.h"

struct fake_qup {
    uint32_t state, mode, bus_error, fifo_error, registers[0x409 / 4 + 1];
    uint8_t output[12], input[84];
    size_t written, read, readable;
    unsigned writes, resets, delayed, state_writes;
    int timeout, invalid_state, unsupported, short_read, fail_state_at, reset_failure;
};

static uint32_t fake_read(void *context, uint32_t offset)
{
    struct fake_qup *fake = context;
    if (offset == LQ_STATE)
        return fake->invalid_state ? 0 : fake->state | 4 | (fake->unsupported ? 0 : 16);
    if (offset == LQ_IO_MODE) return fake->mode;
    if (offset == LQ_I2C_STATUS) return fake->bus_error;
    if (offset == LQ_ERROR) return fake->fifo_error;
    if (offset == LQ_OPERATIONAL) {
        uint32_t flags = fake->timeout ? 0 : 0xc00;
        if (fake->read < fake->readable && !fake->short_read) flags |= 0x20;
        return flags;
    }
    if (offset == LQ_INPUT_FIFO) {
        uint32_t value = 0;
        for (size_t byte = 0; byte < 4; byte++)
            value |= (uint32_t)fake->input[fake->read++] << (8 * byte);
        return value;
    }
    assert(offset / 4 < sizeof(fake->registers) / sizeof(fake->registers[0]));
    return fake->registers[offset / 4];
}

static void fake_write(void *context, uint32_t offset, uint32_t value)
{
    struct fake_qup *fake = context;
    fake->writes++;
    if (offset == LQ_STATE) {
        fake->state_writes++;
        if (fake->state_writes == (unsigned)fake->fail_state_at) return;
        if (fake->reset_failure && value == 0) { fake->invalid_state = 1; return; }
        fake->state = value;
    } else if (offset == LQ_SW_RESET) {
        fake->resets++;
        if (fake->resets == 1 || !fake->reset_failure) fake->state = 0;
    } else if (offset == LQ_OUTPUT_FIFO) {
        assert(fake->state == 3);
        for (size_t byte = 0; byte < 4; byte++) {
            assert(fake->written < sizeof(fake->output));
            fake->output[fake->written++] = (uint8_t)(value >> (byte * 8));
        }
    }
    assert(offset / 4 < sizeof(fake->registers) / sizeof(fake->registers[0]));
    fake->registers[offset / 4] = value;
}

static void fake_delay(void *context, uint32_t microseconds)
{
    struct fake_qup *fake = context;
    fake->delayed += microseconds;
    assert(fake->delayed <= 60000);
}

static struct lilac_qup setup(struct fake_qup *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->mode = (2u << 7) | (2u << 2);
    fake->readable = 82;
    for (unsigned index = 0; index < sizeof(fake->input); index++)
        fake->input[index] = (uint8_t)(index ^ 0x5a);
    return (struct lilac_qup){.context = fake, .read = fake_read,
        .write = fake_write, .delay_us = fake_delay, .clock = 0x35d};
}

int main(void)
{
    struct fake_qup fake;
    struct lilac_qup controller;
    uint8_t index = 0x80, output[80], original[80], page[] = {255, 3};
    controller = setup(&fake);
    assert(lilac_qup_initialize(&controller) && fake.resets == 1 && controller.error == LQ_OK);
    controller = setup(&fake); fake.bus_error = 0x100;
    assert(!lilac_qup_initialize(&controller) && controller.error == LQ_BUSY && !fake.writes);
    controller = setup(&fake); fake.invalid_state = 1;
    assert(!lilac_qup_initialize(&controller) && controller.error == LQ_RESET_FAILED && fake.delayed == 1000);
    for (size_t length = 1; length <= sizeof(output); length++) {
        controller = setup(&fake);
        memset(output, 0xcc, sizeof(output));
        assert(lilac_qup_transfer(&controller, 0x2c, &index, 1, output, length) == LQ_OK);
        assert(!memcmp(output, fake.input + 2, length));
        for (size_t tail = length; tail < sizeof(output); tail++) assert(output[tail] == 0xcc);
        assert(fake.registers[LQ_READ_COUNT / 4] == length + 2);
        assert(fake.registers[LQ_WRITE_COUNT / 4] == 9);
        assert(fake.registers[LQ_CLOCK / 4] == controller.clock && fake.state == 0);
        const uint8_t expected[] = {0x81, 0x58, 0x82, 1, 0x80, 0x81, 0x59, 0x87};
        assert(!memcmp(fake.output, expected, sizeof(expected)) && fake.output[8] == length);
    }
    controller = setup(&fake);
    assert(!lilac_qup_transfer(&controller, 0x2c, page, 2, NULL, 0));
    const uint8_t expected_page[] = {0x81, 0x58, 0x83, 2, 255, 3};
    assert(!memcmp(fake.output, expected_page, sizeof(expected_page)));
    assert(fake.registers[LQ_CONFIG / 4] == 0x287 && !fake.registers[LQ_READ_COUNT / 4]);
    memset(original, 0xcc, sizeof(original));
    for (unsigned fault = 0; fault < 11; fault++) {
        controller = setup(&fake);
        memcpy(output, original, sizeof(output));
        unsigned expected = LQ_OK;
        switch (fault) {
        case 0: fake.bus_error = 0x100; expected = LQ_BUSY; break;
        case 1: fake.mode = 0; expected = LQ_CAPACITY; break;
        case 2: fake.invalid_state = 1; expected = LQ_RESET_FAILED; break;
        case 3: fake.unsupported = 1; expected = LQ_NOT_I2C; break;
        case 4: fake.bus_error = 8; expected = LQ_BUS_ERROR; break;
        case 5: fake.fifo_error = 4; expected = LQ_FIFO_ERROR; break;
        case 6: fake.timeout = 1; expected = LQ_TRANSFER_TIMEOUT; break;
        case 7: fake.short_read = 1; expected = LQ_SHORT_READ; break;
        case 8: fake.fail_state_at = 1; expected = LQ_STATE_TIMEOUT; break;
        case 9: fake.fail_state_at = 2; expected = LQ_STATE_TIMEOUT; break;
        case 10: fake.reset_failure = 1; expected = LQ_RESET_FAILED; break;
        }
        assert(lilac_qup_transfer(&controller, 0x2c, &index, 1, output, 80) == (int)expected);
        assert(controller.error == expected && !memcmp(output, original, sizeof(output)));
        if (fault < 2) assert(fake.writes == 0);
        else if (fault != 2 && fault != 10) assert(fake.state == 0);
    }
    controller = setup(&fake);
    assert(lilac_qup_transfer(&controller, 0x2d, &index, 1, output, 1) == LQ_ARGUMENT);
    assert(lilac_qup_transfer(&controller, 0x2c, &index, 0, output, 1) == LQ_ARGUMENT);
    assert(lilac_qup_transfer(&controller, 0x2c, &index, 1, output, 81) == LQ_ARGUMENT);
    assert(lilac_qup_transfer(&controller, 0x2c, page, 1, output, 1) == LQ_ARGUMENT);
    assert(lilac_qup_transfer(&controller, 0x2c, page, 1, NULL, 0) == LQ_ARGUMENT);
    assert(!fake.writes);
    assert(!lilac_qup_capacity(3, 0) && !lilac_qup_capacity(3 << 5, 1));
    puts("PASS: QUP FIFO ordering, 1..80 byte reads, page writes, bounded faults and atomic output");
}
