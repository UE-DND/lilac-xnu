#!/usr/bin/env python3
"""Standalone Android Boot Image (v0) Packer & Inspect Tool
Specifically tailored for Qualcomm / Sony MSM8998 boot images with appended DTB.
"""
import argparse
import hashlib
from pathlib import Path
import struct


def pad_to(data: bytes, page_size: int) -> bytes:
    pad = (page_size - (len(data) % page_size)) % page_size
    return data + (b'\0' * pad)


def pack_bootimg(kernel: bytes, ramdisk: bytes, dtb: bytes,
                 base: int = 0x00000000,
                 kernel_offset: int = 0x00008000,
                 ramdisk_offset: int = 0x01000000,
                 second_offset: int = 0x00f00000,
                 tags_offset: int = 0x00000100,
                 page_size: int = 4096,
                 cmdline: str = "") -> bytes:
    # Android bootimg v0 header:
    # 0x00: "ANDROID!" (8 bytes)
    # 0x08: kernel_size (uint32)
    # 0x0c: kernel_addr (uint32, base + kernel_offset)
    # 0x10: ramdisk_size (uint32)
    # 0x14: ramdisk_addr (uint32, base + ramdisk_offset)
    # 0x18: second_size (uint32, 0)
    # 0x1c: second_addr (uint32, base + second_offset)
    # 0x20: tags_addr (uint32, base + tags_offset)
    # 0x24: page_size (uint32)
    # 0x28: unused / header_version (uint32, 0)
    # 0x2c: os_version (uint32, 0)
    # 0x30: name (16 bytes, ASCIIZ)
    # 0x40: cmdline (512 bytes, ASCIIZ)
    # 0x240: sha1 (32 bytes)
    # 0x260: extra_cmdline (1024 bytes)

    # In Sony/QC MSM8998, DTB is appended directly to the kernel or passed via dt section.
    # When appended to kernel (as stock Sony does), kernel_payload = kernel + dtb.
    kernel_payload = kernel
    if dtb:
        kernel_payload = kernel + dtb

    k_sz = len(kernel_payload)
    r_sz = len(ramdisk)
    s_sz = 0

    k_addr = base + kernel_offset
    r_addr = (base + ramdisk_offset) if r_sz > 0 else 0
    s_addr = base + second_offset
    t_addr = base + tags_offset

    # Compute SHA1 over kernel, ramdisk, second
    h = hashlib.sha1()
    h.update(kernel_payload)
    h.update(struct.pack('<I', k_sz))
    h.update(ramdisk)
    h.update(struct.pack('<I', r_sz))
    h.update(b'')
    h.update(struct.pack('<I', s_sz))
    sha_digest = h.digest() + (b'\0' * (32 - len(h.digest())))

    cmdline_bytes = cmdline.encode('ascii')[:511] + b'\0'
    cmdline_padded = cmdline_bytes + (b'\0' * (512 - len(cmdline_bytes)))
    extra_cmdline = b'\0' * 1024
    name = b'\0' * 16

    header = struct.pack(
        '<8sIIIIIIIIII16s512s32s1024s',
        b'ANDROID!',
        k_sz,
        k_addr,
        r_sz,
        r_addr,
        s_sz,
        s_addr,
        t_addr,
        page_size,
        0,  # header_version
        0,  # os_version
        name,
        cmdline_padded,
        sha_digest,
        extra_cmdline
    )

    out = pad_to(header, page_size)
    out += pad_to(kernel_payload, page_size)
    if r_sz > 0:
        out += pad_to(ramdisk, page_size)

    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True, help='Path to kernel or BootShim image')
    parser.add_argument('--ramdisk', type=Path, help='Path to ramdisk image')
    parser.add_argument('--dtb', type=Path, help='Path to device tree blob (appended to kernel)')
    parser.add_argument('--cmdline', default='', help='Kernel commandline')
    parser.add_argument('--base', type=lambda s: int(s, 0), default=0x00000000)
    parser.add_argument('--kernel_offset', type=lambda s: int(s, 0), default=0x00008000)
    parser.add_argument('--ramdisk_offset', type=lambda s: int(s, 0), default=0x01000000)
    parser.add_argument('--tags_offset', type=lambda s: int(s, 0), default=0x00000100)
    parser.add_argument('--pagesize', type=int, default=4096)
    parser.add_argument('-o', '--output', type=Path, required=True, help='Output boot.img path')
    args = parser.parse_args()

    k_data = args.kernel.read_bytes()
    r_data = args.ramdisk.read_bytes() if args.ramdisk else b''
    dtb_data = args.dtb.read_bytes() if args.dtb else b''

    img = pack_bootimg(
        k_data, r_data, dtb_data,
        base=args.base,
        kernel_offset=args.kernel_offset,
        ramdisk_offset=args.ramdisk_offset,
        tags_offset=args.tags_offset,
        page_size=args.pagesize,
        cmdline=args.cmdline
    )

    args.output.write_bytes(img)
    print(f'Created {args.output} ({len(img)} bytes)')


if __name__ == '__main__':
    main()
