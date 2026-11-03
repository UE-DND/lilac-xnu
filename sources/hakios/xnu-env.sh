# source this (from bash) before building XNU on Linux
# Everything is found relative to this file. THEOS (default ~/theos) provides the cctools and the
# Darwin SDK; XNU_SRC is the kernel source tree.
DARWIN_REDWOOD=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
export DARWIN_REDWOOD
export XNU_HOST_OS=$(uname)
# On macOS there is no theos: xnu-deps/setup.sh lays out xnu-deps/theos-mac like one (links to the
# cctools of Xcode and to the iPhoneOS 16.5 SDK of github.com/theos/sdks).
if [ "$XNU_HOST_OS" = Darwin ]; then
	export THEOS=${THEOS:-$DARWIN_REDWOOD/xnu-deps/theos-mac}
else
	export THEOS=${THEOS:-$HOME/theos}
fi
export LILAC_ROOT=${LILAC_ROOT:-$(cd "$DARWIN_REDWOOD/../.." && pwd)}
export XNU_SRC=${XNU_SRC:-$LILAC_ROOT/sources/xnu}
export XNUDEPS=${XNUDEPS:-$LILAC_ROOT/out/deps}
export THEOS=${LILAC_THEOS:-$XNUDEPS/theos}
export LILAC_OBJROOT=${LILAC_OBJROOT:-$LILAC_ROOT/out/build/kernel/default/obj}
export LILAC_GENERATED_DIR=${LILAC_GENERATED_DIR:-$LILAC_ROOT/out/build/kernel/default/generated}
export PATH=$XNUDEPS/host/bin:$PATH
export XNU_SDK_PATH=$XNUDEPS/sdk/iPhoneOS16.5.xnu.sdk
export XNU_SDK_VERSION=16.5
export XNU_PLATFORM_PATH=$XNUDEPS/sdk/Platforms/iPhoneOS.platform
