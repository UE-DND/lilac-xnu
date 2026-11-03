#!/bin/bash
# Build XNU for redwood on Linux. Extra make args are passed through.
source "$(dirname "${BASH_SOURCE[0]}")/xnu-env.sh"
cd "$XNU_SRC" || exit 1
# The Wi-Fi driver's object (tools/mk-ath11k.sh) is linked into the kernel when it is there; make
# does not see it change, so the linked kernel is thrown away when it has.
LX=$XNUDEPS/build/ath11k/out/ath11k.o
# ... only into a kernel that has the Wi-Fi driver (CONFIG_HAKIOS_WIFI): the object's calls are answered by hakios.lx.c
if [ -f "$LILAC_GENERATED_DIR/MASTER.hakios" ] && ! grep -q '^#  HAKIOS_DRIVERS = \[.* hakios_wifi ' "$LILAC_GENERATED_DIR/MASTER.hakios"; then LX=/nonexistent; fi
if [ ! -f "$LX" ]; then LX=; fi
# the build tools of SETUP/ are Apple's: on macOS they build as they are, on Linux with a compatibility header
if [ "$XNU_HOST_OS" = Darwin ]; then HOSTCC=clang; else
HOSTCC="clang -include $XNUDEPS/hostinc/hostcompat.h -I$XNUDEPS/hostinc -Wno-unused-command-line-argument -Wno-error=implicit-function-declaration"; fi
exec make SRCROOT="$XNU_SRC" OBJROOT="$LILAC_OBJROOT" SYMROOT="${LILAC_OBJROOT%/obj}/sym" DSTROOT="${LILAC_OBJROOT%/obj}/dst" LILAC_GENERATED_DIR="$LILAC_GENERATED_DIR" HAKIOS_OBJECTS="$LX" SDKROOT=iphoneos ARCH_CONFIGS=ARM64 MACHINE_CONFIGS=${MACHINE:-BCM2837} KERNEL_CONFIGS=DEVELOPMENT \
  ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BUILD_WERROR=0 BUILD_LTO=0 DO_CTFMERGE=0 \
  HOST_CC="$HOSTCC" HOST_FLEX=flex HOST_BISON=bison HOST_GM4=m4 CC=xnu-clang CXX=xnu-clang++ LOGCOLORS=n "$@"
