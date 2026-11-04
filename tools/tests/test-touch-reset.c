#include <assert.h>
#include <stdio.h>
#include "../../sources/xnu/bsd/dev/lilac-touch-reset.h"

struct fixture { uint32_t registers[4]; unsigned writes, waited, fail; };
static uint32_t read_register(void *context, unsigned offset)
{
    return ((struct fixture *)context)->registers[offset];
}
static void write_register(void *context, unsigned offset, uint32_t value)
{
    struct fixture *fixture = context;
    assert(offset == LILAC_TOUCH_RESET_IO && value == 2);
    fixture->writes++;
    if (!fixture->fail) fixture->registers[offset] = value | 1;
}
static void wait_us(void *context, unsigned microseconds)
{
    ((struct fixture *)context)->waited += microseconds;
}
int main(void)
{
    struct fixture fixture = {{0x203, 0, 0x203, 0}, 0, 0, 0};
    struct lilac_touch_reset_io io = {&fixture, read_register, write_register, wait_us};
    assert(!lilac_touch_release_reset(&io));
    assert(fixture.writes == 1 && fixture.waited == 100000);
    assert(!lilac_touch_release_reset(&io) && fixture.writes == 1);
    fixture.registers[LILAC_TOUCH_VDDIO_IO] = 3;
    assert(lilac_touch_release_reset(&io) == 110 && fixture.writes == 1);
    fixture.registers[LILAC_TOUCH_VDDIO_IO] = 0;
    fixture.registers[LILAC_TOUCH_RESET_CONFIG] = 0x20f;
    assert(lilac_touch_release_reset(&io) == 110 && fixture.writes == 1);
    fixture.registers[LILAC_TOUCH_RESET_CONFIG] = 3;
    assert(lilac_touch_release_reset(&io) == 110 && fixture.writes == 1);
    fixture.registers[LILAC_TOUCH_RESET_CONFIG] = 0x203;
    fixture.registers[LILAC_TOUCH_VDDIO_CONFIG] = 3;
    assert(lilac_touch_release_reset(&io) == 110 && fixture.writes == 1);
    fixture.registers[LILAC_TOUCH_VDDIO_CONFIG] = 0x203;
    fixture.registers[LILAC_TOUCH_RESET_IO] = 0;
    fixture.fail = 1;
    assert(lilac_touch_release_reset(&io) == 111 && fixture.writes == 2);
    puts("touch reset: inherited power, polarity, pinmux, idempotence and readback passed");
    return 0;
}
