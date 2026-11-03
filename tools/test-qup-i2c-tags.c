/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/lilac-qup-i2c-tags.h"
int main(void) {
    uint8_t out[9], original[9]; size_t tx=99,rx=99;
    const uint8_t expected[]={0x81,0x58,0x82,1,0x80,0x81,0x59,0x87,80};
    assert(lilac_qup_register_read_tags(0x2c,0x80,80,out,sizeof out,&tx,&rx));
    assert(tx==9 && rx==82 && !memcmp(out,expected,9));
    memcpy(original,out,9); tx=99; rx=99;
    assert(!lilac_qup_register_read_tags(0x80,0,1,out,9,&tx,&rx));
    assert(!lilac_qup_register_read_tags(0x2c,0,0,out,9,&tx,&rx));
    assert(!lilac_qup_register_read_tags(0x2c,0,255,out,9,&tx,&rx));
    assert(!lilac_qup_register_read_tags(0x2c,0,1,out,8,&tx,&rx));
    assert(tx==99 && rx==99 && !memcmp(out,original,9));
    const uint8_t page[]={0x81,0x58,0x83,2,0xff,3};
    assert(lilac_qup_page_select_tags(0x2c,3,out,9,&tx));
    assert(tx==6 && !memcmp(out,page,6));
    puts("PASS: QUP v2 repeated-start read and page-select byte streams; invalid requests rejected");
}
