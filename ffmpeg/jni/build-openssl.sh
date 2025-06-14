#!/bin/bash 
ROOT=$(cd "$(dirname "$0")"; pwd)
source ${ROOT}/util.sh

cd ${ROOT}/openssl-1.0.2s

if test -z ${NDK}
then
    die "ndk not found.Please set NDK environment variable properly."
fi

HOST_PLATFORM=$(probe_host_platform)
FLAVOR=$1

if test -z ${FLAVOR}
then
    die "No flavor selected.Valid architecture:neon tegra2 tegra3 arm64 x86 x86_64"
fi

TOOLCHAIN=${NDK}/toolchains/llvm/prebuilt/${HOST_PLATFORM}

if [ ${FLAVOR} == 'neon' ] || [ ${FLAVOR} == 'tegra2' ] || [ ${FLAVOR} == 'tegra3' ]
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/arm-linux-androideabi-
    TARGET=android-armv7
    CLANG_TARGET="armv7a-none-linux-androideabi21"

elif [ ${FLAVOR} == 'arm64' ] 
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/aarch64-linux-android-
    TARGET=android64-aarch64
    TRIPLE=aarch64-linux-android
    CLANG_TARGET="aarch64-none-linux-android21" 

elif [ ${FLAVOR} == 'x86' ] 
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/i686-linux-android-
    TARGET=android-x86
    CLANG_TARGET="i686-none-linux-android21"

elif [ ${FLAVOR} == 'x86_64' ] 
then
    CROSS_PREFIX=${TOOLCHAIN}/bin/x86_64-linux-android-
    TARGET=android
    CLANG_TARGET="x86_64-none-linux-android21"
else
    die "Unsupported architecture."
fi

if [ ! -e ${CROSS_PREFIX}ar ]; then
  CROSS_PREFIX=${TOOLCHAIN}/bin/llvm-
  fi

export ANDROID_SYSROOT=$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/sysroot
export CC="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang -target $CLANG_TARGET"
export CXX="$NDK/toolchains/llvm/prebuilt/$HOST_PLATFORM/bin/clang++ -target $CLANG_TARGET"
export RANLIB=${CROSS_PREFIX}ranlib
export AR=${CROSS_PREFIX}ar

./Configure ${TARGET}
if test "$?" != 0; then
    die "ERROR: failed to configure openssl.${FLAVOR}"
fi

echo "BUILDING openssl.${FLAVOR}"
make clean; make build_libs -j 4
if test "$?" != 0; then
    die "ERROR: failed to build openssl.${FLAVOR}"
fi
#<<!
DST_DIR=$(get_dst_dir ${FLAVOR})
echo "COPYING openssl.${FLAVOR} to ${DST_DIR}"
if ! test -d  ${DST_DIR}
then
    mkdir -p ${ROOT}/${DST_DIR}
fi
cp libssl.a ${ROOT}/${DST_DIR}
cp libcrypto.a ${ROOT}/${DST_DIR}
#!
