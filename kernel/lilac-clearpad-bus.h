/* SPDX-License-Identifier: GPL-2.0-only
 * RMI over an I2C transport. Each call selects its page afresh; callers must
 * serialize the page write and combined read against all other users.
 */
#ifndef LILAC_CLEARPAD_BUS_H
#define LILAC_CLEARPAD_BUS_H
#include <stddef.h>
#include <stdint.h>

/* Return zero only if the whole transfer completed. For a register read,
 * write_len=1 and read_len>0 require a repeated START with no intervening
 * STOP. A page-select write has read_len=0 and ends with STOP. */
typedef int (*lilac_i2c_xfer)(void *, uint8_t, const uint8_t *, size_t, uint8_t *, size_t);
struct lilac_clearpad_bus { lilac_i2c_xfer xfer; void *ctx; };
static inline int lilac_clearpad_read(void *context, uint16_t address, uint8_t *out, size_t length)
{
    struct lilac_clearpad_bus *bus=context;
    uint8_t page[2]={0xff,(uint8_t)(address>>8)}, reg=(uint8_t)address, result[80];
    if (!bus || !bus->xfer || !out || !length || length>sizeof(result) ||
        reg==0xff || length>255u-reg || page[1]>4) return -1;
    if (bus->xfer(bus->ctx,0x2c,page,2,NULL,0) ||
        bus->xfer(bus->ctx,0x2c,&reg,1,result,length)) return -1;
    for (size_t i=0;i<length;i++) out[i]=result[i];
    return 0;
}
#endif
