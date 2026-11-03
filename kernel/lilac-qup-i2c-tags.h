/* SPDX-License-Identifier: GPL-2.0-only
 * Qualcomm QUP v2 I2C FIFO byte stream for a register read. No MMIO.
 * Tag definitions and combined-message order: Sony i2c-msm-v2.c/.h.
 */
#ifndef LILAC_QUP_I2C_TAGS_H
#define LILAC_QUP_I2C_TAGS_H
#include <stddef.h>
#include <stdint.h>

/* A repeated START separates the register-index write from the read; RX has
 * two tag bytes in addition to the data. Controller setup and error handling
 * remain the caller's responsibility. This does not select an RMI page. */
static inline int lilac_qup_register_read_tags(uint8_t address, uint8_t reg,
    size_t length, uint8_t *out, size_t capacity, size_t *tx, size_t *rx)
{
    if (!out || !tx || !rx || address>0x7f || !length || length>254 || capacity<9) return 0;
    uint8_t tags[9]={0x81,(uint8_t)(address<<1),0x82,1,reg,
        0x81,(uint8_t)((address<<1)|1),0x87,(uint8_t)length};
    for (size_t i=0;i<sizeof(tags);i++) out[i]=tags[i];
    *tx=sizeof(tags); *rx=length+2;
    return 1;
}

/* RMI page-select is a separate I2C write ending in STOP. */
static inline int lilac_qup_page_select_tags(uint8_t address, uint8_t page,
    uint8_t *out, size_t capacity, size_t *tx)
{
    if (!out || !tx || address>0x7f || capacity<6) return 0;
    uint8_t tags[6]={0x81,(uint8_t)(address<<1),0x83,2,0xff,page};
    for (size_t i=0;i<sizeof(tags);i++) out[i]=tags[i];
    *tx=sizeof(tags); return 1;
}
#endif
